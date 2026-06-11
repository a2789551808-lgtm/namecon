#pragma once
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>
#include "MediaServiceImpl.h"

class GrpcServer {
public:
    GrpcServer(const std::string& addr, int port);
    ~GrpcServer();
    void run();   // 阻塞，直到 shutdown
    void stop();

private:
    MediaServiceImpl _service;                  // 必须作为成员，生命周期与 Server 一致
    std::unique_ptr<grpc::Server> _server;
};
