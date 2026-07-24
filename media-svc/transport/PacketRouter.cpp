#include "PacketRouter.h"
#include "UdpServer.h"
#include "IceServer.h"
#include "DtlsContext.h"
#include "SrtpContext.h"
#include "../rtcp/RtcpHandler.h"
#include "../sfu/Peer.h"
#include "../sfu/Router.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>

PacketRouter::PacketRouter(std::shared_ptr<UdpServer> udp)
    : _udp(std::move(udp))
{
}

void PacketRouter::setIceServer(std::shared_ptr<IceServer> ice) {
    _ice = std::move(ice);
}

void PacketRouter::setRtcpHandler(std::shared_ptr<RtcpHandler> rtcp) {
    _rtcp = std::move(rtcp);
}

void PacketRouter::setRouter(std::shared_ptr<Router> router) {
    _router = std::move(router);
}

void PacketRouter::registerPeer(std::shared_ptr<Peer> peer) {
    std::lock_guard<std::mutex> lock(_pendingMutex);
    _pendingPeers[peer->peerId] = peer;
}

std::shared_ptr<Peer> PacketRouter::getOrCreatePeer(
    const boost::asio::ip::udp::endpoint& ep) {
    std::lock_guard<std::mutex> lock(_peerMutex);
    auto it = _peerMap.find(ep);
    if (it != _peerMap.end()) return it->second;

    // 尝试使用预注册的 Peer（AddPeer 时创建）
    std::shared_ptr<Peer> peer;
    {
        std::lock_guard<std::mutex> lock2(_pendingMutex);
        if (!_pendingPeers.empty()) {
            peer = _pendingPeers.begin()->second;
            _pendingPeers.erase(_pendingPeers.begin());
        }
    }
    if (!peer) {
        peer = std::make_shared<Peer>();
    }
    peer->remoteEp = ep;
    _peerMap[ep] = peer;
    return peer;
}

// ==============================
// 协议分类入口（三种协议如何区分）
// ------------------------------
// WebRTC 复用同一个 UDP 端口收发 STUN / DTLS / RTP|RTCP，靠首字节区分：
//   1) STUN: 前 2 bit = 00（即 data[0] == 0x00 且 data[1] <= 0x03），
//            且 offset 4-7 处的 magic cookie == 0x2112A442（最可靠的判据）。
//   2) DTLS: 首字节落在 20~64 之间（ContentType 字段，如 Handshake=22）。
//            注意：DTLS 首字节也可能 >= 20，所以要先排除 STUN 再判 DTLS。
//   3) RTP/RTCP(SRTP): 首字节 >= 128（即版本号 V=2，高 2 bit = 10）。
//                      rtcp-mux 下 RTP 与 RTCP 共用同一端口，由后续 handleSrtp 区分。
// ==============================
// 被谁调用：UdpServer 收到 UDP 数据包后回调本函数。
// 做什么：   根据首字节 + STUN magic cookie 把包分发给 handleStun / handleDtls / handleSrtp。
// 关键参数： data/len —— 收到的原始 UDP 负载；ep —— 发送方 UDP 端点，用于查/建 Peer。
void PacketRouter::onPacket(const uint8_t* data, size_t len,
                            const boost::asio::ip::udp::endpoint& ep) {
    if (len < 1) return;

    uint8_t firstByte = data[0];

    // 先检查 STUN magic cookie（最可靠）：前 2 bit=00 且长度够一个 STUN 头
    if (len >= 20 && data[0] == 0x00 && data[1] <= 0x03) {
        uint32_t cookie;
        memcpy(&cookie, data + 4, 4);
        cookie = ntohl(cookie);
        if (cookie == 0x2112A442) {
            handleStun(data, len, ep);
            return;
        }
    }

    // 排除 STUN 后：20~64 视为 DTLS，>=128 视为 RTP/RTCP(SRTP)
    if (firstByte >= 20 && firstByte <= 64) {
        handleDtls(data, len, ep);
    } else if (firstByte >= 128) {
        handleSrtp(data, len, ep);
    }
}

// ==============================
// STUN -> IceServer
// ==============================
void PacketRouter::handleStun(const uint8_t* data, size_t len,
                              const boost::asio::ip::udp::endpoint& ep) {
    if (_ice) _ice->onStunPacket(data, len, ep);
}

// ==============================
// DTLS -> Peer::dtls
//   首次看到新 endpoint -> 创建 Peer + DtlsContext
//   握手完成 -> 创建 SrtpContext
// ==============================
// 被谁调用：onPacket 把首字节为 20~64 的包判定为 DTLS 后调用本函数。
// 做什么：
//   1) 若该 endpoint 是首次出现，则创建 Peer 并为其新建 DtlsContext，
//      同时注册"DTLS 要回发数据"的回调（通过 _udp->sendTo 回发到该 ep）。
//   2) 把 DTLS 交给 DtlsContext::handlePacket 驱动握手状态机。
//   3) 当握手"刚刚完成"（wasDone=false -> isHandshakeDone()=true）时，
//      从 DTLS 导出 SRTP 密钥并初始化 SrtpContext；SRTP 就绪后调用
//      sendPLItoAllPeers() 向所有已经在推流的 Peer 请求关键帧，
//      使得新加入的 Peer 能尽快拿到可解码的 I 帧开始播放视频。
// 关键参数： data/len -- DTLS 原始包；ep -- 发送方端点，用于 getOrCreatePeer 定位 Peer。
void PacketRouter::handleDtls(const uint8_t* data, size_t len,
                              const boost::asio::ip::udp::endpoint& ep) {
    auto peer = getOrCreatePeer(ep);

    // 首次创建 DtlsContext：注册发送回调，DTLS 握手过程中的回包经 UDP 发回对端
    if (!peer->dtls) {
        peer->dtls = std::make_shared<DtlsContext>();
        peer->dtls->setSendCallback([udp = _udp, ep](const uint8_t* d, size_t l) {
            udp->sendTo(d, l, ep);
        });
        std::cout << "[PacketRouter] New DTLS session for "
                  << ep.address().to_string() << ":" << ep.port() << std::endl;
    }

    // 记录握手"处理前"是否已完成，用于下面判断是否"刚刚完成"
    bool wasDone = peer->dtls->isHandshakeDone();
    // 驱动 DTLS 握手状态机（可能一次处理不完，需要多轮往返）
    peer->dtls->handlePacket(data, len);

    // 握手刚刚完成（wasDone=false 且现在 true）-> 用导出的密钥创建 SRTP 上下文
    if (!wasDone && peer->dtls->isHandshakeDone()) {
        auto srtp = std::make_shared<SrtpContext>();
        // 从 DTLS 握手结果中导出 SRTP 主密钥（SRTP profile 的 keying material）
        std::string keys = peer->dtls->exportSrtpKeys();
        if (!keys.empty() && srtp->init(keys)) {
            peer->srtp = srtp;
            std::cout << "==============================================" << std::endl;
            std::cout << "[PacketRouter] SRTP READY for " << peer->peerId
                      << " @ " << ep.address().to_string() << ":" << ep.port()
                      << std::endl;
            std::cout << "==============================================" << std::endl;

            // 向所有已有 SRTP 的 Peer 发送 PLI，请求关键帧。
            // 新 Peer 刚加入 SFU，尚未持有任何参考帧，只有关键帧(I 帧)才能开始解码，
            // 因此需要立刻让正在推流的发送方重发一个关键帧。
            sendPLItoAllPeers(peer.get());
        } else {
            std::cerr << "[PacketRouter] SRTP init FAILED for "
                      << ep.address().to_string() << ":" << ep.port() << std::endl;
        }
    }
}

// ==============================
// 向所有已有 SRTP 的 Peer 发送 PLI（请求关键帧）
// 新 Peer 加入时调用，mediaSSRC 设为发送方的视频 SSRC
// ==============================
// 被谁调用：handleDtls 在新 Peer 的 SRTP 刚就绪时调用（newPeer 指向新加入者自己）。
// 做什么：
//   遍历所有已建立 SRTP、且已在推视频流的 Peer，向它们逐个发送一条 PLI
//   (Picture Loss Indication, RFC 4585) 报文，要求对方编码器立即产出一个关键帧。
//   PLI 的作用：告诉发送方"我这边画面丢了/刚加入没有参考帧，请发一个 I 帧"。
// mediaSSRC 为什么必须是"视频 SSRC"：
//   PLI 报文里的 mediaSSRC 字段表示"针对哪一路媒体流请求关键帧"，
//   关键帧只对视频有意义，所以必须填该发送方的 videoSsrc；填音频 SSRC 或 0 都无效，
//   对端编码器不会响应。这里用 peer->videoSsrc（发送方此前推流时记录的视频 SSRC）。
// 关键参数： newPeer -- 刚刚加入的 Peer 指针，遍历时跳过它自己，避免给自己发 PLI。
void PacketRouter::sendPLItoAllPeers(Peer* newPeer) {
    std::lock_guard<std::mutex> lock(_peerMutex);
    for (auto& [ep, peer] : _peerMap) {
        if (peer.get() == newPeer) continue;          // 跳过新加入者自己
        if (!peer->srtp) continue;                     // 该 Peer 尚未完成 DTLS/SRTP，发不了
        if (peer->videoSsrc == 0) continue;  // 还没收到过这个 Peer 的视频流，没有可请求的 SSRC

        // PLI 包结构 (12 字节, RFC 4585)
        //   byte0: V=2,P=0,FMT=1 -> 0x81；byte1: PT=PSFB=206
        uint8_t pli[12];
        pli[0] = 0x81;  // V=2, P=0, FMT=1
        pli[1] = 206;   // PT=PSFB
        uint16_t pliLen = htons(2);
        memcpy(pli + 2, &pliLen, 2);
        uint32_t senderSsrc = 0;  // SFU 自身 SSRC，这里用 0（SFU 不作为媒体发送方）
        memcpy(pli + 4, &senderSsrc, 4);
        // mediaSSRC 必须是发送方的"视频 SSRC"，对端才会针对视频流生成关键帧
        uint32_t mediaSsrc = htonl(peer->videoSsrc);  // 发送方的视频 SSRC
        memcpy(pli + 8, &mediaSsrc, 4);

        // PLI 也要经过 SRTP 加密(protect)再发送，否则浏览器会丢弃
        uint8_t out[64];
        memcpy(out, pli, 12);
        int outLen = 12;
        if (peer->srtp->protectRtcp(out, &outLen)) {
            _udp->sendTo(out, static_cast<size_t>(outLen), peer->remoteEp);
            std::cout << "[PacketRouter] Sent PLI to " << peer->peerId
                      << " (videoSsrc=" << peer->videoSsrc << ")" << std::endl;
        }
    }
}

// ==============================
// SRTP -> Peer::srtp 解密 -> RTP/RTCP 分发
// ==============================
// 被谁调用：onPacket 把首字节 >= 128 的包判定为 RTP/RTCP(SRTP) 后调用。
// 做什么：用该 Peer 的 SrtpContext 对收到的密文做 SRTP 解密(unprotect)，
//         解出明文后区分 RTP / RTCP 分别交给 Router / RtcpHandler 处理；
//         若解出的是 RTCP 且为 PLI，则继续调用 forwardPLI 把它转发给发送方。
// 为什么先试 RTP 再试 RTCP 解密：
//   WebRTC 默认 rtcp-mux，RTP 与 RTCP 共用同一端口、同一 SSRC 空间，
//   SRTP 对 RTP 和 RTCP 使用不同的认证标签/序号空间（unprotect vs unprotectRtcp）。
//   首字节的高 6 bit（PT）并不能 100% 区分二者（RTP payload type 与 RTCP PT 有重叠区），
//   因此这里采用"试错"策略：先用 unprotect(RTP) 解，失败说明不是 RTP，
//   再重新拷贝原始数据用 unprotectRtcp(RTCP) 解。这样能兼容 mux 下的混合流量。
// 关键参数： data/len -- SRTP 密文；ep -- 发送方端点，用于定位 Peer 及其 srtp 上下文。
void PacketRouter::handleSrtp(const uint8_t* data, size_t len,
                              const boost::asio::ip::udp::endpoint& ep) {
    auto peer = getOrCreatePeer(ep);
    if (!peer->srtp) {
        // DTLS 握手未完成或失败，还没有 SRTP 上下文，无法解密
        std::cerr << "[PacketRouter] No SRTP context for "
                  << ep.address().to_string() << ":" << ep.port() << std::endl;
        return;
    }

    // 复制一份到可写缓冲区，unprotect 是原地解密
    uint8_t buf[65536];
    memcpy(buf, data, len);
    int bufLen = static_cast<int>(len);

    // 先试 RTP 解密，失败则试 RTCP 解密（rtcp-mux 下 RTP/RTCP 共用端口）
    // RTP 与 RTCP 在 SRTP 中是两个独立的密钥流/序号空间，必须分别尝试。
    if (peer->srtp->unprotect(buf, &bufLen)) {
        // RTP 包解密成功 -> 交给 Router 转发给其他订阅者
        if (_router) {
            _router->onRtpPacket(buf, static_cast<size_t>(bufLen), peer.get());
        }
    } else {
        // RTP 解密失败：可能是 RTCP。需要用原始密文重新拷贝后再试 RTCP 解密
        // （上一步 unprotect 可能已改动 buf 内容，所以要 memcpy 还原）
        memcpy(buf, data, len);
        bufLen = static_cast<int>(len);
        if (peer->srtp->unprotectRtcp(buf, &bufLen)) {
            // RTCP 包 -> 交给 RTCP 处理器（NACK/RR/SR 等）
            if (_rtcp) {
                _rtcp->onRtcpPacket(buf, static_cast<size_t>(bufLen));
            }
            // 转发 PLI 给发送方：检查解密后的包是否是 PLI 包
            // PLI 判据：byte[0] & 0x1F == 1 (FMT=1) 且 byte[1] == 206 (PT=PSFB)
            if (bufLen >= 12 && (buf[0] & 0x1F) == 1 && buf[1] == 206) {
                uint32_t mediaSsrc;
                memcpy(&mediaSsrc, buf + 8, 4);
                mediaSsrc = ntohl(mediaSsrc);
                // 找到这个 SSRC 对应的发送方，把 PLI 转发给他（SFU 代理转发）
                forwardPLI(mediaSsrc, peer.get());
            }
        }
    }
}

// ==============================
// 转发浏览器发的 PLI 给发送方
// 通过 mediaSSRC 查 RouteTable 精确找到发送方，只发给那一个人
// ==============================
// 被谁调用：handleSrtp 解出 RTCP 包并判定为 PLI 时调用。
// 为什么转发浏览器的 PLI：
//   浏览器(接收端)检测到画面花屏/丢包严重时会自己发出 PLI，请求发送方给一个关键帧。
//   但浏览器只把 PLI 发给了它直连的 SFU，SFU 必须把这条请求继续中继给真正的"视频发送方"，
//   否则发送方不知道有接收端需要关键帧，画面就一直恢复不了。
//   SFU 在这里是"代理转发"角色：把接收端的 PLI 透传给对应的发送端。
// 做什么：
//   1) 用 PLI 里的 mediaSSRC 在 Router 的路由表里反查到视频发送方 Peer；
//   2) 重新构造一条 PLI（SFU 作为发送方，senderSSRC=0），用发送方的 SRTP 加密后发给他。
// 关键参数：
//   mediaSsrc -- PLI 报文中"被请求的媒体 SSRC"，即视频发送方的 SSRC，用于定位发送方；
//   receiver  -- 发出这条 PLI 的接收端 Peer（仅用于日志/上下文，本函数不直接用它发包）。
void PacketRouter::forwardPLI(uint32_t mediaSsrc, Peer* receiver) {
    if (!_router) return;

    // 通过 SSRC 在路由表中反查发送方 Peer
    Peer* sender = _router->findPeerBySsrc(mediaSsrc);
    if (!sender) {
        std::cerr << "[PacketRouter] PLI: sender not found for SSRC=" << mediaSsrc << std::endl;
        return;
    }
    if (!sender->srtp) return;  // 发送方还没建立 SRTP，无法加密发送

    // 构造 PLI 包（与 sendPLItoAllPeers 相同的 12 字节结构）
    uint8_t pli[12];
    pli[0] = 0x81;  // V=2, P=0, FMT=1
    pli[1] = 206;   // PT=PSFB
    uint16_t pliLen = htons(2);
    memcpy(pli + 2, &pliLen, 2);
    uint32_t senderSsrc = 0;  // SFU SSRC = 0（SFU 代理发送，不使用自己的 SSRC）
    memcpy(pli + 4, &senderSsrc, 4);
    uint32_t netSsrc = htonl(mediaSsrc);
    memcpy(pli + 8, &netSsrc, 4);

    // 用发送方的 SRTP 上下文加密后，只发给这一个发送方
    uint8_t out[64];
    memcpy(out, pli, 12);
    int outLen = 12;
    if (sender->srtp->protectRtcp(out, &outLen)) {
        _udp->sendTo(out, static_cast<size_t>(outLen), sender->remoteEp);
        static int pliCount = 0;
        // 限流日志，避免刷屏
        if (++pliCount <= 10) {
            std::cout << "[PacketRouter] Forwarded PLI to " << sender->peerId
                      << " (mediaSsrc=" << mediaSsrc << ")" << std::endl;
        }
    }
}
