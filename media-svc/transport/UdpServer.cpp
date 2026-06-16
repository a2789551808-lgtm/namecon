#include "UdpServer.h"
#include <iostream>

UdpServer::UdpServer(boost::asio::io_context& ioc, uint16_t port)
    : _socket(ioc, boost::asio::ip::udp::endpoint(
        boost::asio::ip::udp::v4(), port))
    , _port(port)
{
    std::cout << "[UdpServer] Bound to port " << port << std::endl;
}

UdpServer::~UdpServer() {
    boost::system::error_code ec;
    _socket.close(ec);
}

void UdpServer::startReceive(ReceiveCallback callback) {
    _onReceive = std::move(callback);
    doReceive();
}

void UdpServer::doReceive() {
    _socket.async_receive_from(
        boost::asio::buffer(_recvBuffer, sizeof(_recvBuffer)),
        _remoteEp,
        [this](const boost::system::error_code& ec, size_t bytesRead) {
            onReceive(ec, bytesRead);
        });
}

void UdpServer::onReceive(const boost::system::error_code& ec, size_t bytesRead) {
    if (ec) {
        std::cerr << "[UdpServer] Receive error: " << ec.message() << std::endl;
        return;
    }

    if (_onReceive) {
        _onReceive(_recvBuffer, bytesRead, _remoteEp);
    }
    doReceive();
}

void UdpServer::sendTo(const uint8_t* data, size_t len,
                       const boost::asio::ip::udp::endpoint& target) {
    _socket.async_send_to(
        boost::asio::buffer(data, len), target,
        [](const boost::system::error_code& ec, size_t /*sent*/) {
            if (ec) {
                std::cerr << "[UdpServer] Send error: " << ec.message() << std::endl;
            }
        });
}
