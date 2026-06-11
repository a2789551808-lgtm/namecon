#include "config/ConfigMgr.h"
#include "grpc/GrpcServer.h"
#include "transport/UdpServer.h"

#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

int main(int argc, char* argv[]) {
    try {
        std::cout << "NameCon media-svc starting..." << std::endl;

        // 加载配置（单例，全局可访问）
        std::string configPath = "configs/media-svc.yaml";
        if (argc > 1) {
            configPath = argv[1];
        }
        ConfigMgr::GetInstance().Load(configPath);
        auto& cfg = ConfigMgr::GetInstance();

        // 启动 gRPC Server
        auto server = std::make_unique<GrpcServer>("0.0.0.0",
            std::stoi(cfg["server"]["grpc_port"]));

        // asio 事件循环 — 单 io_context，多线程跑 run() 利用多核
        boost::asio::io_context ioc;

        // UDP Server — 收浏览器发来的 SRTP 加密包
        uint16_t udpPort = static_cast<uint16_t>(
            std::stoi(cfg["server"]["udp_port_start"]));
        auto udp = std::make_shared<UdpServer>(ioc, udpPort);
        udp->startReceive([](const uint8_t* data, size_t len, auto& ep) {
            // TODO Phase 2: 这里将来会解密→路由→加密→转发
            std::cout << "[UDP] Received " << len << " bytes from "
                      << ep.address().to_string() << ":" << ep.port() << std::endl;
        });

        // 信号处理
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code& error, int) {
            if (!error) {
                std::cout << "\n[main] Shutting down..." << std::endl;
                server->stop();
                ioc.stop();
            }
        });

        // 多线程跑事件循环 — 5 个工作线程
        constexpr unsigned int kThreadCount = 5;
        std::vector<std::thread> threads;
        for (unsigned int i = 0; i < kThreadCount; ++i) {
            threads.emplace_back([&ioc] { ioc.run(); });
        }
        std::cout << "[main] io_context running on " << kThreadCount
                  << " threads" << std::endl;

        for (auto& t : threads) t.join();
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[main] Fatal: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
