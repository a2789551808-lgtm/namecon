#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>

class UdpServer;
class IceServer;
class DtlsContext;
class SrtpContext;

// UDP 收包分发器 — 按协议类型路由
//   STUN (高2bit=00) → IceServer
//   DTLS (首字节20~64) → DtlsContext (自动按 endpoint 管理)
//   SRTP (首字节128+) → SrtpContext (DTLS 完成后自动创建)
class PacketRouter {
public:
    PacketRouter(std::shared_ptr<UdpServer> udp);
    void setIceServer(std::shared_ptr<IceServer> ice);
    void onPacket(const uint8_t* data, size_t len,
                  const boost::asio::ip::udp::endpoint& ep);

private:
    void handleStun(const uint8_t* d, size_t l, const boost::asio::ip::udp::endpoint& ep);
    void handleDtls(const uint8_t* d, size_t l, const boost::asio::ip::udp::endpoint& ep);
    void handleSrtp(const uint8_t* d, size_t l, const boost::asio::ip::udp::endpoint& ep);

    std::shared_ptr<UdpServer> _udp;
    std::shared_ptr<IceServer> _ice;

    std::unordered_map<boost::asio::ip::udp::endpoint, std::shared_ptr<DtlsContext>> _dtlsMap;
    std::mutex _dtlsMutex;
    std::unordered_map<boost::asio::ip::udp::endpoint, std::shared_ptr<SrtpContext>> _srtpMap;
    std::mutex _srtpMutex;
};