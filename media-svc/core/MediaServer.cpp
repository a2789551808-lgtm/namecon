#include "MediaServer.h"
#include "../config/ConfigMgr.h"
#include "../grpc/GrpcServer.h"
#include "../transport/UdpServer.h"
#include "../transport/IceServer.h"
#include "../transport/DtlsContext.h"
#include "../transport/PacketRouter.h"
#include "../rtcp/RtcpHandler.h"
#include "../sfu/Router.h"
#include "../sfu/RouteTable.h"

#include <boost/asio.hpp>
#include <iostream>
#include <thread>
#include <vector>

MediaServer::MediaServer(const std::string& configPath)
    : _configPath(configPath)
{
    // ① 加载配置
    ConfigMgr::GetInstance().Load(_configPath);

    // ② 生成 DTLS 自签证书
    _dtlsFingerprint = DtlsContext::initGlobals();
    std::cout << "[main] DTLS fingerprint: " << _dtlsFingerprint << std::endl;

    // ③ asio
    _ioc = std::make_unique<boost::asio::io_context>();

    // ④ 组装所有组件
    setupComponents();
    setupSignals();
}

MediaServer::~MediaServer() {
    stop();
}

void MediaServer::setupComponents() {
    auto& cfg = ConfigMgr::GetInstance();

    // gRPC
    _grpcServer = std::make_unique<GrpcServer>("0.0.0.0",
        std::stoi(cfg["server"]["grpc_port"]));

    // UDP
    uint16_t udpPort = static_cast<uint16_t>(
        std::stoi(cfg["server"]["udp_port_start"]));
    _udp = std::make_shared<UdpServer>(*_ioc, udpPort);

    // ICE
    _ice = std::make_shared<IceServer>();
    _ice->setSendCallback([udp = _udp](const uint8_t* data, size_t len,
                                        const boost::asio::ip::udp::endpoint& target) {
        udp->sendTo(data, len, target);
    });

    // SFU Router
    _routeTable = std::make_shared<RouteTable>();
    _router = std::make_shared<Router>(_udp, _routeTable);

    // RTCP
    _rtcp = std::make_shared<RtcpHandler>();
    _rtcp->setSendCallback([this](const uint8_t* /*data*/, size_t /*len*/) {
        // RTCP 包需走 SRTP 加密，通过具体 Peer 的 SrtpContext 发送
        // 后续在 Router 回调中处理
    });

    // 收包分发
    _pktRouter = std::make_shared<PacketRouter>(_udp);
    _pktRouter->setIceServer(_ice);
    _pktRouter->setRtcpHandler(_rtcp);
    _pktRouter->setRouter(_router);

    _udp->startReceive([this](const uint8_t* data, size_t len,
                               const boost::asio::ip::udp::endpoint& ep) {
        _pktRouter->onPacket(data, len, ep);
    });
}

void MediaServer::setupSignals() {
    auto signals = std::make_shared<boost::asio::signal_set>(
        *_ioc, SIGINT, SIGTERM);
    signals->async_wait([this](const boost::system::error_code& error, int) {
        if (!error) {
            std::cout << "\n[main] Shutting down..." << std::endl;
            stop();
        }
    });
}

void MediaServer::run() {
    std::cout << "NameCon media-svc starting..." << std::endl;

    constexpr unsigned int kThreadCount = 5;
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([this] { _ioc->run(); });
    }
    std::cout << "[main] io_context running on " << kThreadCount
              << " threads" << std::endl;

    for (auto& t : threads) t.join();
}

void MediaServer::stop() {
    if (_grpcServer) _grpcServer->stop();
    if (_ioc) _ioc->stop();
}
