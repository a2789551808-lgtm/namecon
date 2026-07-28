#include "DtlsContext.h"
#include "../utils/Logger.h"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/rand.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <iostream>

// 全局静态成员
SSL_CTX*    DtlsContext::_ctx = nullptr;
std::string DtlsContext::_certFingerprint;

// ============================================================
// 全局初始化 — 生成自签证书 + 计算指纹
// ============================================================

std::string DtlsContext::initGlobals(const std::string& certFile,
                                      const std::string& keyFile) {
    // TODO: 支持从文件加载已有证书
    if (!certFile.empty() && !keyFile.empty()) {
    }

    // ① 生成 ECDSA 密钥 (P-256)
    EVP_PKEY* pkey = EVP_PKEY_new();
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    EVP_PKEY_keygen_init(pctx);
    EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_X9_62_prime256v1);
    EVP_PKEY_keygen(pctx, &pkey);
    EVP_PKEY_CTX_free(pctx);

    // ② 生成自签 X509 证书（有效期 1 年）
    X509* cert = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert),  365 * 24 * 3600);
    X509_set_pubkey(cert, pkey);
    X509_sign(cert, pkey, EVP_sha256());

    // ③ 计算 SHA-256 指纹（格式: "AB:CD:EF:01:...")
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int  mdLen = 0;
    X509_digest(cert, EVP_sha256(), md, &mdLen);

    std::ostringstream fp;
    for (unsigned int i = 0; i < mdLen; ++i) {
        if (i > 0) fp << ":";
        fp << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(md[i]);
    }
    _certFingerprint = fp.str();

    // ④ 创建全局 SSL_CTX (DTLS 1.2)
    _ctx = SSL_CTX_new(DTLS_method());
    SSL_CTX_set_min_proto_version(_ctx, DTLS1_VERSION);
    SSL_CTX_set_max_proto_version(_ctx, DTLS1_2_VERSION);
    SSL_CTX_use_certificate(_ctx, cert);
    SSL_CTX_use_PrivateKey(_ctx, pkey);

    // ⑤ 设置 SRTP profile
    SSL_CTX_set_tlsext_use_srtp(_ctx, "SRTP_AES128_CM_SHA1_80");

    X509_free(cert);
    EVP_PKEY_free(pkey);

    LOG_INFO("Cert fingerprint: {}", _certFingerprint);
    return _certFingerprint;
}

void DtlsContext::cleanupGlobals() {
    if (_ctx) { SSL_CTX_free(_ctx); _ctx = nullptr; }
}

// ============================================================
// 每个 Peer 实例
// ============================================================

DtlsContext::DtlsContext()
    : _ssl(nullptr)
    , _readBIO(nullptr)
    , _writeBIO(nullptr)
    , _handshakeDone(false)
{
    _ssl = SSL_new(_ctx);
    _readBIO  = BIO_new(BIO_s_mem());
    _writeBIO = BIO_new(BIO_s_mem());
    SSL_set_bio(_ssl, _readBIO, _writeBIO);
    SSL_set_accept_state(_ssl);  // SFU 被动，浏览器主动
}

DtlsContext::~DtlsContext() {
    if (_ssl) { SSL_free(_ssl); _ssl = nullptr; }
}

void DtlsContext::setSendCallback(SendCallback cb) {
    _sendCb = std::move(cb);
}

bool DtlsContext::handlePacket(const uint8_t* data, size_t len) {
    if (_handshakeDone) return true;

    // ① 喂给 OpenSSL
    BIO_write(_readBIO, data, static_cast<int>(len));

    // ② 触发握手
    int ret = SSL_do_handshake(_ssl);
    if (ret == 1) {
        _handshakeDone = true;
        LOG_INFO("Handshake done!");
    }

    // ③ 取出 OpenSSL 要发的数据
    uint8_t out[4096];
    int n;
    while ((n = BIO_read(_writeBIO, out, sizeof(out))) > 0) {
        if (_sendCb) _sendCb(out, static_cast<size_t>(n));
    }
    return _handshakeDone;
}

std::string DtlsContext::exportSrtpKeys() const {
    unsigned char material[60];
    if (!SSL_export_keying_material(
            const_cast<SSL*>(_ssl),
            material, sizeof(material),
            "EXTRACTOR-dtls_srtp", 19,
            nullptr, 0, 0)) {
        return "";
    }
    return std::string(reinterpret_cast<char*>(material), sizeof(material));
}
