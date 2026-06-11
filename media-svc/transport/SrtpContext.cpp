#include "SrtpContext.h"
#include <cstring>
#include <iostream>

SrtpContext::SrtpContext()
    : _srtpIn(nullptr)
    , _srtpOut(nullptr)
    , _inited(false)
{
    // libsrtp 库初始化（全局只做一次，重复调用无害）
    srtp_init();
}

SrtpContext::~SrtpContext() {
    if (_srtpIn)  srtp_dealloc(_srtpIn);
    if (_srtpOut) srtp_dealloc(_srtpOut);
}

bool SrtpContext::init(const std::string& keyMaterial) {
    if (keyMaterial.size() < 60) {
        std::cerr << "[SrtpContext] keyMaterial too short: "
                  << keyMaterial.size() << " (need 60)" << std::endl;
        return false;
    }

    const uint8_t* km = reinterpret_cast<const uint8_t*>(keyMaterial.data());

    // 提取密钥和盐
    const uint8_t* clientKey   = km;        // [0..15]
    const uint8_t* serverKey   = km + 16;   // [16..31]
    const uint8_t* clientSalt  = km + 32;   // [32..45]
    const uint8_t* serverSalt  = km + 46;   // [46..59]

    // === 解密策略（浏览器→SFU，用 client key） ===
    srtp_policy_t policyIn{};
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policyIn.rtp);
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policyIn.rtcp);
    policyIn.ssrc.type  = ssrc_any_inbound;   // 接受任意 SSRC
    policyIn.key        = const_cast<uint8_t*>(clientKey);
    policyIn.window_size = 128;               // 抗重放窗口
    policyIn.allow_repeat_tx = 0;

    // 把 salt 拼到 key 后面（libsrtp 要求 key+salt 连续存放）
    uint8_t keyWithSaltIn[30];
    memcpy(keyWithSaltIn, clientKey, 16);
    memcpy(keyWithSaltIn + 16, clientSalt, 14);
    policyIn.key = keyWithSaltIn;

    srtp_err_status_t stat = srtp_create(&_srtpIn, &policyIn);
    if (stat != srtp_err_status_ok) {
        std::cerr << "[SrtpContext] srtp_create(in) failed: " << stat << std::endl;
        return false;
    }

    // === 加密策略（SFU→浏览器，用 server key） ===
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
        std::cerr << "[SrtpContext] srtp_create(out) failed: " << stat << std::endl;
        srtp_dealloc(_srtpIn);
        _srtpIn = nullptr;
        return false;
    }

    _inited = true;
    std::cout << "[SrtpContext] SRTP initialized (AES-128-CM, SHA1-80)" << std::endl;
    return true;
}

bool SrtpContext::unprotect(uint8_t* buf, int* len) {
    if (!_inited || !_srtpIn) return false;
    int origLen = *len;
    srtp_err_status_t stat = srtp_unprotect(_srtpIn, buf, len);
    if (stat != srtp_err_status_ok) {
        std::cerr << "[SrtpContext] unprotect failed: " << stat
                  << " (len=" << origLen << ")" << std::endl;
        return false;
    }
    return true;
}

bool SrtpContext::protect(uint8_t* buf, int* len) {
    if (!_inited || !_srtpOut) return false;
    srtp_err_status_t stat = srtp_protect(_srtpOut, buf, len);
    if (stat != srtp_err_status_ok) {
        std::cerr << "[SrtpContext] protect failed: " << stat << std::endl;
        return false;
    }
    return true;
}
