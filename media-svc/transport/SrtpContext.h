#pragma once
#include <srtp2/srtp.h>
#include <string>
#include <cstdint>

// SRTP 加解密上下文 — 每个 Peer 一个实例
// 用 DTLS 握手导出的密钥材料初始化
class SrtpContext {
public:
    SrtpContext();
    ~SrtpContext();

    // 用 DtlsContext::exportSrtpKeys() 返回的 60 字节初始化
    // 密钥布局:
    //   [0..15]   client_write_key   (16B) → SFU 解密收到的包
    //   [16..31]  server_write_key   (16B) → SFU 加密发出的包
    //   [32..45]  client_write_salt  (14B)
    //   [46..59]  server_write_salt  (14B)
    bool init(const std::string& keyMaterial);

    // 解密收到的 SRTP 包 → 明文 RTP（长度不变，去掉 SRTP auth tag）
    bool unprotect(uint8_t* buf, int* len);

    // 加密明文 RTP → SRTP（长度增加 SRTP auth tag 10 字节）
    bool protect(uint8_t* buf, int* len);

private:
    srtp_t _srtpIn;   // 解密上下文（浏览器→SFU）
    srtp_t _srtpOut;  // 加密上下文（SFU→浏览器）
    bool _inited;
};
