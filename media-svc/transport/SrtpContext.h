#pragma once
#include <srtp2/srtp.h>
#include <string>
#include <cstdint>

// SRTP 加解密上下文 - 每个 Peer 一个实例
// 用 DTLS 握手导出的密钥材料初始化
class SrtpContext {
public:
    SrtpContext();
    ~SrtpContext();

    // 用 DtlsContext::exportSrtpKeys() 返回的 60 字节初始化
    bool init(const std::string& keyMaterial);

    // 解密收到的 SRTP RTP 包 -> 明文 RTP
    bool unprotect(uint8_t* buf, int* len);

    // 解密收到的 SRTCP 包 -> 明文 RTCP
    bool unprotectRtcp(uint8_t* buf, int* len);

    // 加密明文 RTP -> SRTP
    bool protect(uint8_t* buf, int* len);

    // 加密明文 RTCP -> SRTCP
    bool protectRtcp(uint8_t* buf, int* len);

private:
    srtp_t _srtpIn;   // 解密上下文（浏览器->SFU）
    srtp_t _srtpOut;  // 加密上下文（SFU->浏览器）
    bool _inited;
};
