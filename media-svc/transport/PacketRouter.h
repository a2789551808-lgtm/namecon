#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>

class UdpServer;
class IceServer;
class RtcpHandler;
struct Peer;
class Router;

// UDP 收包分发器 — 按协议类型路由
//   STUN (高2bit=00) → IceServer
//   DTLS (首字节20~64) → Peer::dtls (自动按 endpoint 管理)
//   SRTP (首字节128+) → Peer::srtp 解密 → RTP→Router / RTCP→RtcpHandler
class PacketRouter {
public:
    PacketRouter(std::shared_ptr<UdpServer> udp);

    void setIceServer(std::shared_ptr<IceServer> ice);
    void setRtcpHandler(std::shared_ptr<RtcpHandler> rtcp);
    void setRouter(std::shared_ptr<Router> router);

    void onPacket(const uint8_t* data, size_t len,
                  const boost::asio::ip::udp::endpoint& ep);

    // 预注册 Peer（MediaServiceImpl::AddPeer 调用）
    void registerPeer(std::shared_ptr<Peer> peer);

    // 获取或创建 Peer
    std::shared_ptr<Peer> getOrCreatePeer(
        const boost::asio::ip::udp::endpoint& ep);

private:
    void handleStun(const uint8_t* d, size_t l, const boost::asio::ip::udp::endpoint& ep);
    void handleDtls(const uint8_t* d, size_t l, const boost::asio::ip::udp::endpoint& ep);
    void handleSrtp(const uint8_t* d, size_t l, const boost::asio::ip::udp::endpoint& ep);
    void sendPLItoAllPeers(Peer* newPeer);
    void forwardPLI(uint32_t mediaSsrc, Peer* receiver);

    std::shared_ptr<UdpServer>    _udp;
    std::shared_ptr<IceServer>    _ice;
    std::shared_ptr<RtcpHandler>  _rtcp;
    std::shared_ptr<Router>       _router;

    // endpoint → Peer（取代旧 _dtlsMap + _srtpMap）
    std::unordered_map<boost::asio::ip::udp::endpoint,
                       std::shared_ptr<Peer>> _peerMap;
    std::mutex _peerMutex;

    // AddPeer 预注册的 Peer（等待 DTLS 连接绑定 endpoint）
    std::unordered_map<std::string, std::shared_ptr<Peer>> _pendingPeers;
    std::mutex _pendingMutex;
};