#include "PacketRouter.h"
#include "UdpServer.h"
#include "IceServer.h"
#include "DtlsContext.h"
#include "SrtpContext.h"
#include "../rtcp/RtcpHandler.h"
#include <iostream>

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

// ==============================
// 协议分类入口
// ==============================
void PacketRouter::onPacket(const uint8_t* data, size_t len,
                            const boost::asio::ip::udp::endpoint& ep) {
    if (len < 1) return;

    uint8_t firstByte = data[0];

    if ((firstByte & 0xC0) == 0) {                    // STUN: 高2bit=00
        handleStun(data, len, ep);
    } else if (firstByte >= 20 && firstByte <= 64) {  // DTLS: 类型 20~64
        handleDtls(data, len, ep);
    } else if (firstByte >= 128) {                    // SRTP: 首字节 128+
        handleSrtp(data, len, ep);
    }
}

// ==============================
// STUN 包 → IceServer
// ==============================
void PacketRouter::handleStun(const uint8_t* data, size_t len,
                              const boost::asio::ip::udp::endpoint& ep) {
    if (_ice) {
        _ice->onStunPacket(data, len, ep);
    }
}

// ==============================
// DTLS 握手 → DtlsContext::onReceive
// 握手完成后自动创建 SrtpContext
// ==============================
void PacketRouter::handleDtls(const uint8_t* data, size_t len,
                              const boost::asio::ip::udp::endpoint& ep) {
    // 1. 获取或创建 DtlsContext
    std::shared_ptr<DtlsContext> dtls;
    bool wasNew = false;
    {
        std::lock_guard<std::mutex> lock(_dtlsMutex);
        auto it = _dtlsMap.find(ep);
        if (it == _dtlsMap.end()) {
            dtls = std::make_shared<DtlsContext>();
            dtls->setSendCallback([udp = _udp, ep](const uint8_t* d, size_t l) {
                udp->sendTo(d, l, ep);
            });
            _dtlsMap[ep] = dtls;
            wasNew = true;
            std::cout << "[PacketRouter] New DTLS session: "
                      << ep.address().to_string() << ":" << ep.port() << std::endl;
        } else {
            dtls = it->second;
        }
    }

    // 2. 喂数据给 DTLS
    dtls->handlePacket(data, len);

    // 3. 如果之前没完成、现在完成了 → 导出密钥 → 创建 SrtpContext
    bool wasDone = (wasNew ? false : true);  // 新 session 还没完成
    if (!wasDone && dtls->isHandshakeDone()) {
        auto srtp = std::make_shared<SrtpContext>();
        std::string keys = dtls->exportSrtpKeys();
        if (!keys.empty() && srtp->init(keys)) {
            std::lock_guard<std::mutex> lock2(_srtpMutex);
            _srtpMap[ep] = srtp;
            std::cout << "[PacketRouter] SRTP ready for "
                      << ep.address().to_string() << ":" << ep.port() << std::endl;
        }
    }
}

// ==============================
// SRTP 媒体包 → SrtpContext::unprotect → RTP/RTCP 分发
// ==============================
void PacketRouter::handleSrtp(const uint8_t* data, size_t len,
                              const boost::asio::ip::udp::endpoint& ep) {
    std::shared_ptr<SrtpContext> srtp;
    {
        std::lock_guard<std::mutex> lock(_srtpMutex);
        auto it = _srtpMap.find(ep);
        if (it != _srtpMap.end()) srtp = it->second;
    }
    if (!srtp) {
        std::cerr << "[PacketRouter] No SRTP context for "
                  << ep.address().to_string() << ":" << ep.port() << std::endl;
        return;
    }

    // 复制到可修改 buffer
    uint8_t buf[65536];
    memcpy(buf, data, len);
    int bufLen = static_cast<int>(len);

    if (!srtp->unprotect(buf, &bufLen)) return;

    // 解密成功 → 通过 PT 字节区分 RTP / RTCP
    //   RTP:  PT < 128  (0-127)
    //   RTCP: PT 200-223 → 发给 RtcpHandler
    if (bufLen >= 2) {
        uint8_t pt = buf[1];
        if (pt >= 200 && pt <= 223 && _rtcp) {
            _rtcp->onRtcpPacket(buf, static_cast<size_t>(bufLen));
        } else {
            // ✅ 明文 RTP — 🔲 Day 16-17: Router → 查表 → 转发给其他 Peer
        }
    }
}