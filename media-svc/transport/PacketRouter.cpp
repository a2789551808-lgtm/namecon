#include "PacketRouter.h"
#include "UdpServer.h"
#include "IceServer.h"
#include "DtlsContext.h"

PacketRouter::PacketRouter(std::shared_ptr<UdpServer> udp)
    : _udp(std::move(udp))
{
}

void PacketRouter::setIceServer(std::shared_ptr<IceServer> ice) {
    _ice = std::move(ice);
}

void PacketRouter::onPacket(const uint8_t* data, size_t len,
                            const boost::asio::ip::udp::endpoint& ep) {
    if (len < 2) return;

    uint8_t firstByte = data[0];

    if ((firstByte & 0xC0) == 0) {             // STUN
        handleStun(data, len, ep);
    } else if (firstByte >= 20 && firstByte <= 64) {  // DTLS
        handleDtls(data, len, ep);
    }
    // else: SRTP (128+) — 🔲 待实现
}

void PacketRouter::handleStun(const uint8_t* data, size_t len,
                              const boost::asio::ip::udp::endpoint& ep) {
    if (_ice) {
        _ice->onStunPacket(data, len, ep);
    }
}

void PacketRouter::handleDtls(const uint8_t* data, size_t len,
                              const boost::asio::ip::udp::endpoint& ep) {
    std::shared_ptr<DtlsContext> dtls;
    {
        std::lock_guard<std::mutex> lock(_dtlsMutex);
        auto it = _dtlsMap.find(ep);
        if (it == _dtlsMap.end()) {
            dtls = std::make_shared<DtlsContext>();
            // DTLS 要发数据时，通过 UdpServer 发回同一个 endpoint
            dtls->setSendCallback([udp = _udp, ep](const uint8_t* d, size_t l) {
                udp->sendTo(d, l, ep);
            });
            _dtlsMap[ep] = dtls;
        } else {
            dtls = it->second;
        }
    }
    dtls->handlePacket(data, len);
}