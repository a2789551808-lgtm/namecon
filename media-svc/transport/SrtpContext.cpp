#include "SrtpContext.h"
#include "../utils/Logger.h"
#include <cstring>
#include <iostream>

SrtpContext::SrtpContext()
    : _srtpIn(nullptr)
    , _srtpOut(nullptr)
    , _inited(false)
{
    srtp_init();
}

SrtpContext::~SrtpContext() {
    if (_srtpIn)  srtp_dealloc(_srtpIn);
    if (_srtpOut) srtp_dealloc(_srtpOut);
}

// init：用 DTLS 握手协商出的 SRTP 密钥材料初始化加解密上下文
// 做什么：从 keyMaterial 中切分出 client/server 的密钥与盐值，分别创建
//         入站解密（_srtpIn）和出站加密（_srtpOut）两个 srtp_t 会话。
// 参数含义：
//   keyMaterial - DTLS 握手导出的 60 字节密钥材料，布局如下：
//     [ 0..15 ]  clientKey  (16B)  浏览器->SFU 方向的加密密钥
//     [16..31 ]  serverKey  (16B)  SFU->浏览器 方向的加密密钥
//     [32..45 ]  clientSalt (14B)  浏览器->SFU 方向的盐值
//     [46..59 ]  serverSalt (14B)  SFU->浏览器 方向的盐值
//   libsrtp 要求密钥+盐拼接成 30 字节，所以每个方向都把对应密钥(16B)
//   和盐(14B)拷贝到 30 字节缓冲再传给 srtp_create。
// 为什么有两个 srtp_t：
//   SRTP 是单向的——同一个 srtp_t 只能做"加密"或"解密"之一。WebRTC 中
//   浏览器用 clientKey 加密发送、SFU 用 clientKey 解密接收；SFU 用 serverKey
//   加密发送、浏览器用 serverKey 解密接收。因此 _srtpIn（ssrc_any_inbound）
//   用于解密浏览器发来的包，_srtpOut（ssrc_any_outbound）用于加密发给浏览器的包。
bool SrtpContext::init(const std::string& keyMaterial) {
    if (keyMaterial.size() < 60) {
        LOG_ERROR("keyMaterial too short: {} (need 60)", keyMaterial.size());
        return false;
    }

    const uint8_t* km = reinterpret_cast<const uint8_t*>(keyMaterial.data());

    const uint8_t* clientKey  = km;
    const uint8_t* serverKey  = km + 16;
    const uint8_t* clientSalt = km + 32;
    const uint8_t* serverSalt = km + 46;

    // 解密策略（浏览器->SFU，用 client key）
    srtp_policy_t policyIn{};
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policyIn.rtp);
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policyIn.rtcp);
    policyIn.ssrc.type  = ssrc_any_inbound;
    policyIn.window_size = 128;
    policyIn.allow_repeat_tx = 0;

    uint8_t keyWithSaltIn[30];
    memcpy(keyWithSaltIn, clientKey, 16);
    memcpy(keyWithSaltIn + 16, clientSalt, 14);
    policyIn.key = keyWithSaltIn;

    srtp_err_status_t stat = srtp_create(&_srtpIn, &policyIn);
    if (stat != srtp_err_status_ok) {
        LOG_ERROR("srtp_create(in) failed: {}", stat);
        return false;
    }

    // 加密策略（SFU->浏览器，用 server key）
    srtp_policy_t policyOut{};
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policyOut.rtp);
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policyOut.rtcp);
    policyOut.ssrc.type  = ssrc_any_outbound;
    policyOut.window_size = 128;
    policyOut.allow_repeat_tx = 0;

    uint8_t keyWithSaltOut[30];
    memcpy(keyWithSaltOut, serverKey, 16);
    memcpy(keyWithSaltOut + 16, serverSalt, 14);
    policyOut.key = keyWithSaltOut;

    stat = srtp_create(&_srtpOut, &policyOut);
    if (stat != srtp_err_status_ok) {
        LOG_ERROR("srtp_create(out) failed: {}", stat);
        srtp_dealloc(_srtpIn);
        _srtpIn = nullptr;
        return false;
    }

    _inited = true;
    LOG_INFO("SRTP initialized (AES-128-CM, SHA1-80)");
    return true;
}

// unprotect：解密 RTP 包（入站方向，使用 _srtpIn）
// 做什么：调用 srtp_unprotect 去掉浏览器发来的 RTP 包的加密和认证标签，
//         还原出明文 RTP 供 SFU 处理。
// 参数含义：
//   buf - 输入为密文 RTP，输出为明文 RTP（就地修改）
//   len - 输入为密文长度，输出会被改成解密后明文长度
// 为什么 RTP 和 RTCP 要用不同的解密函数：
//   RTP 包头固定以版本号+负载类型开头，而 RTCP 包头以接收报告/发送报告等
//   类型字段开头，两者的包头格式、加密时 IV 计算方式、认证标签位置都不同。
//   libsrtp 内部对 RTP 用 srtp_unprotect、对 RTCP 用 srtp_unprotect_rtcp，
//   它们各自按对应协议的格式解析包头和校验认证标签，不能混用。
bool SrtpContext::unprotect(uint8_t* buf, int* len) {
    if (!_inited || !_srtpIn) return false;
    srtp_err_status_t stat = srtp_unprotect(_srtpIn, buf, len);
    return stat == srtp_err_status_ok;
}

// unprotectRtcp：解密 RTCP 包（入站方向，使用 _srtpIn）
// 做什么：调用 srtp_unprotect_rtcp 去掉浏览器发来的 RTCP 包的加密和认证标签，
//         还原出明文 RTCP 供 SFU 处理。
// 参数含义：
//   buf - 输入为密文 RTCP，输出为明文 RTCP（就地修改）
//   len - 输入为密文长度，输出会被改成解密后明文长度
// 与 unprotect 的区别：见上方 unprotect 的说明，RTP 和 RTCP 包头格式不同，
//   必须使用各自专用的解密函数。
bool SrtpContext::unprotectRtcp(uint8_t* buf, int* len) {
    if (!_inited || !_srtpIn) return false;
    srtp_err_status_t stat = srtp_unprotect_rtcp(_srtpIn, buf, len);
    return stat == srtp_err_status_ok;
}

// protect：加密 RTP 包（出站方向，使用 _srtpOut）
// 做什么：调用 srtp_protect 给明文 RTP 包加上加密和认证标签，生成密文后
//         由 SFU 通过 UDP 发给目标浏览器。
// 参数含义：
//   buf - 输入为明文 RTP，输出为密文 RTP（就地修改）
//   len - 输入为明文长度，输出会被改成加密后密文长度（通常多出认证标签字节数）
// 为什么 RTP 和 RTCP 要用不同的加密函数：
//   与解密同理，RTP 和 RTCP 包头格式不同，加密时 IV 推导方式和认证标签
//   添加位置也不同，必须分别用 srtp_protect 和 srtp_protect_rtcp。
bool SrtpContext::protect(uint8_t* buf, int* len) {
    if (!_inited || !_srtpOut) return false;
    srtp_err_status_t stat = srtp_protect(_srtpOut, buf, len);
    return stat == srtp_err_status_ok;
}

// protectRtcp：加密 RTCP 包（出站方向，使用 _srtpOut）
// 做什么：调用 srtp_protect_rtcp 给明文 RTCP 包加上加密和认证标签，
//         生成密文后由 SFU 发给目标浏览器。
// 参数含义：
//   buf - 输入为明文 RTCP，输出为密文 RTCP（就地修改）
//   len - 输入为明文长度，输出会被改成加密后密文长度
// 与 protect 的区别：见上方 protect 的说明，RTP 和 RTCP 包头格式不同，
//   必须使用各自专用的加密函数。
bool SrtpContext::protectRtcp(uint8_t* buf, int* len) {
    if (!_inited || !_srtpOut) return false;
    srtp_err_status_t stat = srtp_protect_rtcp(_srtpOut, buf, len);
    return stat == srtp_err_status_ok;
}
