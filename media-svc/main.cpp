#include "config/Config.h"
#include "grpc/GrpcServer.h"

#include <iostream>
#include <memory>

#include <boost/asio.hpp>

int main(int argc, char* argv[]) {
    try {
        std::cout << "NameCon media-svc starting..." << std::endl;

        // 加载配置（单例，全局可访问）
        std::string configPath = "configs/media-svc.yaml";
        if (argc > 1) {
            configPath = argv[1];
        }
        ConfigMgr::Init(configPath);
        auto cfg = ConfigMgr::Inst();

        // 启动 gRPC Server
        auto server = std::make_unique<GrpcServer>("0.0.0.0", cfg->grpc_port);

        // asio 事件循环（Phase 2 UDP 层也会用它）
        boost::asio::io_context ioc{1};

        // 信号处理 — 无需全局变量
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code& error, int /*sig*/) {
            if (!error) {
                std::cout << "\n[main] Shutting down..." << std::endl;
                server->stop();
                ioc.stop();
            }
        });

        ioc.run();   // 阻塞，等待信号

        std::cout << "[main] media-svc stopped" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[main] Fatal: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
