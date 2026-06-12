#pragma once
#include <memory>
#include <string>
#include <boost/asio.hpp>

class UdpServer;
class IceServer;
class RtcpHandler;
class PacketRouter;
class Router;
class RouteTable;
class GrpcServer;

// C++ 媒体服务总控 — 创建并串联所有组件
// main.cpp 只管: new MediaServer → run()
class MediaServer {
public:
    MediaServer(const std::string& configPath);
    ~MediaServer();

    void run();
    void stop();

private:
    void setupComponents();
    void setupSignals();

    std::string _configPath;
    std::string _dtlsFingerprint;

    // boost::asio
    std::unique_ptr<boost::asio::io_context> _ioc;

    // 组件
    std::shared_ptr<UdpServer>    _udp;
    std::shared_ptr<IceServer>    _ice;
    std::shared_ptr<RouteTable>   _routeTable;
    std::shared_ptr<Router>       _router;
    std::shared_ptr<RtcpHandler>  _rtcp;
    std::shared_ptr<PacketRouter> _pktRouter;

    // gRPC
    std::unique_ptr<GrpcServer> _grpcServer;
};
