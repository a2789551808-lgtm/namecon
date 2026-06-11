#include "config/ConfigMgr.h"
#include "grpc/GrpcServer.h"
#include "transport/UdpServer.h"
#include "transport/IceServer.h"
#include "transport/DtlsContext.h"
#include "transport/PacketRouter.h"

#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

int main(int argc, char* argv[]) {
    try {
        std::cout << "NameCon media-svc starting..." << std::endl;

        // 加载配置
        std::string configPath = "configs/media-svc.yaml";
        if (argc > 1) configPath = argv[1];
        ConfigMgr::GetInstance().Load(configPath);
        auto& cfg = ConfigMgr::GetInstance();

        // 生成 DTLS 自签证书
        std::string fingerprint = DtlsContext::initGlobals();
        std::cout << "[main] DTLS fingerprint: " << fingerprint << std::endl;

        // 启动 gRPC Server
        auto server = std::make_unique<GrpcServer>("0.0.0.0",
            std::stoi(cfg["server"]["grpc_port"]));

        // asio 事件循环
        boost::asio::io_context ioc;

        // UDP Server
        uint16_t udpPort = static_cast<uint16_t>(
            std::stoi(cfg["server"]["udp_port_start"]));
        auto udp = std::make_shared<UdpServer>(ioc, udpPort);

        // ICE-lite (STUN)
        auto ice = std::make_shared<IceServer>();
        ice->setSendCallback([udp](const uint8_t* data, size_t len,
                                   const boost::asio::ip::udp::endpoint& target) {
            udp->sendTo(data, len, target);
        });

        // 收包分发器 — STUN/DTLS/SRTP 路由
        PacketRouter router(udp);
        router.setIceServer(ice);
        udp->startReceive([&router](const uint8_t* data, size_t len,
                                     const boost::asio::ip::udp::endpoint& ep) {
            router.onPacket(data, len, ep);
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

        // 5 线程事件循环
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
