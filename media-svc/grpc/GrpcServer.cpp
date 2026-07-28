#include "GrpcServer.h"
#include <iostream>
#include "../utils/Logger.h"

GrpcServer::GrpcServer(const std::string& addr, int port) {
    grpc::ServerBuilder builder;
    std::string listenAddr = addr + ":" + std::to_string(port);
    builder.AddListeningPort(listenAddr, grpc::InsecureServerCredentials());
    builder.RegisterService(&_service);

    _server = builder.BuildAndStart();
    LOG_INFO("Server listening on {}", listenAddr);
}

GrpcServer::~GrpcServer() {
    stop();
}

void GrpcServer::run() {
    if (_server) {
        _server->Wait();
    }
}

void GrpcServer::stop() {
    if (_server) {
        _server->Shutdown();
        _server->Wait();
        LOG_INFO("Server stopped");
    }
}
