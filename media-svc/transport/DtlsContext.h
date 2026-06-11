#pragma once
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <string>
#include <functional>

// DTLS 握手上下文 — 每个 Peer 一个实例
// 使用 OpenSSL Memory BIO 模式：UDP 收发数据通过 BIO 读写中转
class DtlsContext {
public:
    using SendCallback = std::function<void(
        const uint8_t* data, size_t len)>;

    DtlsContext();
    ~DtlsContext();

    // === 全局：启动时调用一次 ===
    // 生成自签证书 + 初始化 SSL_CTX，返回 SHA-256 指纹（用于 SDP Answer）
    static std::string initGlobals(const std::string& certFile = "",
                                    const std::string& keyFile  = "");
    static void cleanupGlobals();

    // === 每个 Peer ===
    void setSendCallback(SendCallback cb);

    // 喂入收到的 DTLS 握手包，返回 true 表示握手完成
    bool handlePacket(const uint8_t* data, size_t len);

    // 握手是否已完成
    bool isHandshakeDone() const { return _handshakeDone; }

    // 导出 SRTP 密钥材料（握手完成后调用）
    // 返回 60 字节: client_write_key(16) + server_write_key(16) + client_salt(14) + server_salt(14)
    std::string exportSrtpKeys() const;

private:
    void doHandshake();

    SSL* _ssl;
    BIO* _readBIO;   // OpenSSL 从这里读 → 我们把收到的 UDP 数据写进来
    BIO* _writeBIO;  // OpenSSL 往这里写 → 我们读出来通过 UDP 发出
    bool _handshakeDone;
    SendCallback _sendCb;

    static SSL_CTX* _ctx;   // 全局共享的 DTLS 上下文
    static std::string _certFingerprint;  // 证书 SHA-256 指纹
};
