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
// 协议分类入口
// STUN: 前2bit=00, 且 magic cookie (offset 4-7) = 0x2112A442
// DTLS: 首字节 20-64 (但前2bit=00, 可能被STUN误判)
// RTP/RTCP: 首字节 >= 128
// ==============================
void PacketRouter::onPacket(const uint8_t* data, size_t len,
                            const boost::asio::ip::udp::endpoint& ep) {
    if (len < 1) return;

    uint8_t firstByte = data[0];

    // 先检查 STUN magic cookie（最可靠）
    if (len >= 20 && data[0] == 0x00 && data[1] <= 0x03) {
        uint32_t cookie;
        memcpy(&cookie, data + 4, 4);
        cookie = ntohl(cookie);
        if (cookie == 0x2112A442) {
            handleStun(data, len, ep);
            return;
        }
    }

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
void PacketRouter::handleDtls(const uint8_t* data, size_t len,
                              const boost::asio::ip::udp::endpoint& ep) {
    auto peer = getOrCreatePeer(ep);

    // 首次创建 DtlsContext
    if (!peer->dtls) {
        peer->dtls = std::make_shared<DtlsContext>();
        peer->dtls->setSendCallback([udp = _udp, ep](const uint8_t* d, size_t l) {
            udp->sendTo(d, l, ep);
        });
        std::cout << "[PacketRouter] New DTLS session for "
                  << ep.address().to_string() << ":" << ep.port() << std::endl;
    }

    bool wasDone = peer->dtls->isHandshakeDone();
    peer->dtls->handlePacket(data, len);

    // 握手刚刚完成 -> 创建 SRTP
    if (!wasDone && peer->dtls->isHandshakeDone()) {
        auto srtp = std::make_shared<SrtpContext>();
        std::string keys = peer->dtls->exportSrtpKeys();
        if (!keys.empty() && srtp->init(keys)) {
            peer->srtp = srtp;
            std::cout << "==============================================" << std::endl;
            std::cout << "[PacketRouter] SRTP READY for " << peer->peerId
                      << " @ " << ep.address().to_string() << ":" << ep.port()
                      << std::endl;
            std::cout << "==============================================" << std::endl;

            // 向所有已有 SRTP 的 Peer 发送 PLI，请求关键帧
            // 新 Peer 加入后需要关键帧才能解码视频
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
void PacketRouter::sendPLItoAllPeers(Peer* newPeer) {
    std::lock_guard<std::mutex> lock(_peerMutex);
    for (auto& [ep, peer] : _peerMap) {
        if (peer.get() == newPeer) continue;
        if (!peer->srtp) continue;
        if (peer->videoSsrc == 0) continue;  // 还没收到过这个 Peer 的视频流

        // PLI 包结构 (12 字节, RFC 4585)
        uint8_t pli[12];
        pli[0] = 0x81;  // V=2, P=0, FMT=1
        pli[1] = 206;   // PT=PSFB
        uint16_t pliLen = htons(2);
        memcpy(pli + 2, &pliLen, 2);
        uint32_t senderSsrc = 0;  // SFU SSRC = 0
        memcpy(pli + 4, &senderSsrc, 4);
        uint32_t mediaSsrc = htonl(peer->videoSsrc);  // 发送方的视频 SSRC
        memcpy(pli + 8, &mediaSsrc, 4);

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
void PacketRouter::handleSrtp(const uint8_t* data, size_t len,
                              const boost::asio::ip::udp::endpoint& ep) {
    auto peer = getOrCreatePeer(ep);
    if (!peer->srtp) {
        std::cerr << "[PacketRouter] No SRTP context for "
                  << ep.address().to_string() << ":" << ep.port() << std::endl;
        return;
    }

    uint8_t buf[65536];
    memcpy(buf, data, len);
    int bufLen = static_cast<int>(len);

    // 先试 RTP 解密，失败则试 RTCP 解密（rtcp-mux 下 RTP/RTCP 共用端口）
    if (peer->srtp->unprotect(buf, &bufLen)) {
        // RTP 包 -> 转发
        if (_router) {
            _router->onRtpPacket(buf, static_cast<size_t>(bufLen), peer.get());
        }
    } else {
        // 可能是 RTCP，重新尝试
        memcpy(buf, data, len);
        bufLen = static_cast<int>(len);
        if (peer->srtp->unprotectRtcp(buf, &bufLen)) {
            if (_rtcp) {
                _rtcp->onRtcpPacket(buf, static_cast<size_t>(bufLen));
            }
            // 转发 PLI 给发送方：检查是否是 PLI 包
            // PLI: byte[0] & 0x1F = 1 (FMT), byte[1] = 206 (PT=PSFB)
            if (bufLen >= 12 && (buf[0] & 0x1F) == 1 && buf[1] == 206) {
                uint32_t mediaSsrc;
                memcpy(&mediaSsrc, buf + 8, 4);
                mediaSsrc = ntohl(mediaSsrc);
                // 找到这个 SSRC 对应的发送方，把 PLI 转发给他
                forwardPLI(mediaSsrc, peer.get());
            }
        }
    }
}

// ==============================
// 转发浏览器发的 PLI 给发送方
// 通过 mediaSSRC 查 RouteTable 精确找到发送方，只发给那一个人
// ==============================
void PacketRouter::forwardPLI(uint32_t mediaSsrc, Peer* receiver) {
    if (!_router) return;

    // 通过 SSRC 查找发送方 Peer
    Peer* sender = _router->findPeerBySsrc(mediaSsrc);
    if (!sender) {
        std::cerr << "[PacketRouter] PLI: sender not found for SSRC=" << mediaSsrc << std::endl;
        return;
    }
    if (!sender->srtp) return;

    // 构造 PLI 包
    uint8_t pli[12];
    pli[0] = 0x81;  // V=2, P=0, FMT=1
    pli[1] = 206;   // PT=PSFB
    uint16_t pliLen = htons(2);
    memcpy(pli + 2, &pliLen, 2);
    uint32_t senderSsrc = 0;  // SFU SSRC = 0
    memcpy(pli + 4, &senderSsrc, 4);
    uint32_t netSsrc = htonl(mediaSsrc);
    memcpy(pli + 8, &netSsrc, 4);

    // 只发给发送方
    uint8_t out[64];
    memcpy(out, pli, 12);
    int outLen = 12;
    if (sender->srtp->protectRtcp(out, &outLen)) {
        _udp->sendTo(out, static_cast<size_t>(outLen), sender->remoteEp);
        static int pliCount = 0;
        if (++pliCount <= 10) {
            std::cout << "[PacketRouter] Forwarded PLI to " << sender->peerId
                      << " (mediaSsrc=" << mediaSsrc << ")" << std::endl;
        }
    }
}
