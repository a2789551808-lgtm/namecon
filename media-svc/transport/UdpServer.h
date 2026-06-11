#pragma once
#include <boost/asio.hpp>
#include <functional>
#include <string>
#include <cstdint>

// 异步 UDP 服务器 — 单 socket 处理所有 Peer
// 绑定到 io_context 池中的一个，收包后回调给上层
class UdpServer {
public:
    using ReceiveCallback = std::function<void(
        const uint8_t* data, size_t len,
        const boost::asio::ip::udp::endpoint& remote)>;

    UdpServer(boost::asio::io_context& ioc, uint16_t port);
    ~UdpServer();

    // 启动异步接收循环
    void startReceive(ReceiveCallback callback);

    // 异步发送数据到指定 endpoint
    void sendTo(const uint8_t* data, size_t len,
                const boost::asio::ip::udp::endpoint& target);

    uint16_t getPort() const { return _port; }

private:
    void doReceive();
    void onReceive(const boost::system::error_code& ec, size_t bytesRead);

    boost::asio::ip::udp::socket _socket;
    uint16_t _port;
    uint8_t _recvBuffer[65536];
    boost::asio::ip::udp::endpoint _remoteEp;
    ReceiveCallback _onReceive;
};
