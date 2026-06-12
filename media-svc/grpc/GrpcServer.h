#pragma once
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>
#include "MediaServiceImpl.h"

class Router;
class IceServer;
class PacketRouter;

class GrpcServer {
public:
    GrpcServer(const std::string& addr, int port);
    ~GrpcServer();
    void run();
    void stop();

    // 注入 SFU 依赖 → 转发给 MediaServiceImpl
    void setRouter(std::shared_ptr<Router> r)           { _service.setRouter(std::move(r)); }
    void setIceServer(std::shared_ptr<IceServer> i)     { _service.setIceServer(std::move(i)); }
    void setPacketRouter(std::shared_ptr<PacketRouter> p) { _service.setPacketRouter(std::move(p)); }

private:
    MediaServiceImpl _service;
    std::unique_ptr<grpc::Server> _server;
};
