#include "GrpcServer.h"
#include <iostream>

GrpcServer::GrpcServer(const std::string& addr, int port) {
    grpc::ServerBuilder builder;
    std::string listenAddr = addr + ":" + std::to_string(port);
    builder.AddListeningPort(listenAddr, grpc::InsecureServerCredentials());
    builder.RegisterService(&_service);

    _server = builder.BuildAndStart();
    std::cout << "[gRPC] Server listening on " << listenAddr << std::endl;
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
        std::cout << "[gRPC] Server stopped" << std::endl;
    }
}
