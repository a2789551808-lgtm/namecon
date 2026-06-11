#include "config/ConfigMgr.h"
#include "grpc/GrpcServer.h"
#include "transport/UdpServer.h"
#include "transport/IceServer.h"

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

        // UDP Server — 收浏览器发来的所有 UDP 包
        uint16_t udpPort = static_cast<uint16_t>(
            std::stoi(cfg["server"]["udp_port_start"]));
        auto udp = std::make_shared<UdpServer>(ioc, udpPort);

        // ICE-lite Server — 处理 STUN Binding Request  [✅ 已测试]
        auto ice = std::make_shared<IceServer>();
        ice->setSendCallback([udp](const uint8_t* data, size_t len,
                                   const boost::asio::ip::udp::endpoint& target) {
            udp->sendTo(data, len, target);
        });

        // UDP 收包 → 按类型分发（STUN / DTLS / SRTP）
        udp->startReceive([ice](const uint8_t* data, size_t len,
                                 const boost::asio::ip::udp::endpoint& ep) {
            if (len < 2) return;

            // 判断包类型（根据第一个字节的高 2 bit）
            //   00 = STUN     (RFC 5389)
            //   01 = 未使用
            //   10 = 未使用
            //   11 = RTP/RTCP  (非 DTLS)，或 DTLS (高字节 20~64)
            uint8_t firstByte = data[0];
            bool isStun = (firstByte & 0xC0) == 0;

            if (isStun) {
                // ✅ STUN 包 — 已通过 Python 脚本验证
                //    Binding Request → Binding Response + XOR-MAPPED-ADDRESS
                ice->onStunPacket(data, len, ep);
                return;
            }

            // 🔲 DTLS 握手包 — 待 Day 11-13 实现 DtlsContext
            // 🔲 SRTP 加密的 RTP/RTCP — 待 Day 11-15 实现
            /*
            std::cout << "[UDP] Non-STUN " << len << " bytes from "
                      << ep.address().to_string() << ":" << ep.port()
                      << " (DTLS or SRTP, not yet handled)" << std::endl;
            */
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
