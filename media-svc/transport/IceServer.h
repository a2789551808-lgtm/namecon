#pragma once
#include <boost/asio.hpp>
#include <string>

// ICE-lite 服务端 — 处理 STUN Binding Request
// 浏览器必须收到 STUN Response 才会继续连接
class IceServer {
public:
    using SendCallback = std::function<void(
        const uint8_t* data, size_t len,
        const boost::asio::ip::udp::endpoint& target)>;

    IceServer();
    void setSendCallback(SendCallback cb);

    // 处理收到的 STUN 包，如果是 Binding Request 就发送 Response
    void onStunPacket(const uint8_t* data, size_t len,
                      const boost::asio::ip::udp::endpoint& remote);

    std::string getIceUfrag() const { return _iceUfrag; }
    std::string getIcePwd() const   { return _icePwd; }

private:
    std::string _iceUfrag;
    std::string _icePwd;
    SendCallback _sendCb;
};
