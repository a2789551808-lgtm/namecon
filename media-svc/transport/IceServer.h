#pragma once
#include <boost/asio.hpp>
#include <string>

// ICE-lite 服务端 — 处理 STUN Binding Request
// 验证 ice-ufrag + ice-pwd (MESSAGE-INTEGRITY) 确认请求来自持有 SDP 的合法客户端
class IceServer {
public:
    using SendCallback = std::function<void(
        const uint8_t* data, size_t len,
        const boost::asio::ip::udp::endpoint& target)>;

    IceServer();
    void setSendCallback(SendCallback cb);

    // 处理收到的 STUN 包
    //   → 校验 USERNAME (含 ufrag) 和 MESSAGE-INTEGRITY (HMAC-SHA1 with pwd)
    //   → 通过后发送 Binding Response (含 XOR-MAPPED-ADDRESS)
    void onStunPacket(const uint8_t* data, size_t len,
                      const boost::asio::ip::udp::endpoint& remote);

    std::string getIceUfrag() const { return _iceUfrag; }
    std::string getIcePwd() const   { return _icePwd; }

private:
    // 验证 STUN 凭据: USERNAME(含 ufrag) + MESSAGE-INTEGRITY(含 pwd 的 HMAC)
    // 返回 true 表示请求合法
    bool validateCredentials(const uint8_t* data, size_t len,
                             std::string& outPeerUfrag);

    // 构造 Binding Response + MESSAGE-INTEGRITY
    void sendResponse(const uint8_t* request, size_t reqLen,
                      const boost::asio::ip::udp::endpoint& remote);

    std::string _iceUfrag;
    std::string _icePwd;
    SendCallback _sendCb;
};
