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

std::shared_ptr<Peer> PacketRouter::getOrCreatePeer(
    const boost::asio::ip::udp::endpoint& ep) {
    std::lock_guard<std::mutex> lock(_peerMutex);
    auto it = _peerMap.find(ep);
    if (it != _peerMap.end()) return it->second;

    auto peer = std::make_shared<Peer>();
    peer->remoteEp = ep;
    _peerMap[ep] = peer;
    return peer;
}

// ==============================
// 协议分类入口
// ==============================
void PacketRouter::onPacket(const uint8_t* data, size_t len,
                            const boost::asio::ip::udp::endpoint& ep) {
    if (len < 1) return;

    uint8_t firstByte = data[0];

    if ((firstByte & 0xC0) == 0) {
        handleStun(data, len, ep);
    } else if (firstByte >= 20 && firstByte <= 64) {
        handleDtls(data, len, ep);
    } else if (firstByte >= 128) {
        handleSrtp(data, len, ep);
    }
}

// ==============================
// STUN → IceServer
// ==============================
void PacketRouter::handleStun(const uint8_t* data, size_t len,
                              const boost::asio::ip::udp::endpoint& ep) {
    if (_ice) _ice->onStunPacket(data, len, ep);
}

// ==============================
// DTLS → Peer::dtls
//   首次看到新 endpoint → 创建 Peer + DtlsContext
//   握手完成 → 创建 SrtpContext
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

    // 握手刚刚完成 → 创建 SRTP
    if (!wasDone && peer->dtls->isHandshakeDone()) {
        auto srtp = std::make_shared<SrtpContext>();
        std::string keys = peer->dtls->exportSrtpKeys();
        if (!keys.empty() && srtp->init(keys)) {
            peer->srtp = srtp;
            std::cout << "[PacketRouter] SRTP ready for "
                      << ep.address().to_string() << ":" << ep.port() << std::endl;
        }
    }
}

// ==============================
// SRTP → Peer::srtp 解密 → RTP/RTCP 分发
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

    if (!peer->srtp->unprotect(buf, &bufLen)) return;

    // 区分 RTP / RTCP
    if (bufLen >= 2) {
        uint8_t pt = buf[1];
        if (pt >= 200 && pt <= 223 && _rtcp) {
            _rtcp->onRtcpPacket(buf, static_cast<size_t>(bufLen));
        } else if (_router) {
            // ✅ 明文 RTP → Router 转发
            _router->onRtpPacket(buf, static_cast<size_t>(bufLen), peer.get());
        }
    }
}