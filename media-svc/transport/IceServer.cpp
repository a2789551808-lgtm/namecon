#include "IceServer.h"
#include <cstring>
#include <ctime>
#include <random>
#include <arpa/inet.h>
#include <openssl/hmac.h>
#include <zlib.h>
#include <iostream>

// STUN 消息头 (20 字节)
static const uint32_t STUN_MAGIC_COOKIE      = 0x2112A442;
static const uint16_t BINDING_REQUEST        = 0x0001;
static const uint16_t BINDING_RESPONSE       = 0x0101;
static const uint16_t ATTR_XOR_MAPPED        = 0x0020;
static const uint16_t ATTR_USERNAME          = 0x0006;
static const uint16_t ATTR_MESSAGE_INTEGRITY = 0x0008;
static const uint16_t ATTR_FINGERPRINT       = 0x8028;
static const size_t   STUN_HEADER_SIZE       = 20;

// 随机字符串
static std::string randomString(size_t len) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static std::mt19937 gen(std::time(nullptr));
    std::uniform_int_distribution<> dis(0, sizeof(chars) - 2);
    std::string s;
    for (size_t i = 0; i < len; ++i) s += chars[dis(gen)];
    return s;
}

IceServer::IceServer()
    : _iceUfrag(randomString(4))
    , _icePwd(randomString(22))
{
    std::cout << "[IceServer] ufrag=" << _iceUfrag
              << " pwd=" << _icePwd << std::endl;
}

void IceServer::setSendCallback(SendCallback cb) {
    _sendCb = std::move(cb);
}

// ============================================================
// 出入口
// ============================================================
void IceServer::onStunPacket(const uint8_t* data, size_t len,
                             const boost::asio::ip::udp::endpoint& remote) {
    if (len < STUN_HEADER_SIZE) return;

    uint16_t msgType;
    memcpy(&msgType, data, 2);
    msgType = ntohs(msgType);
    if (msgType != BINDING_REQUEST) return;

    uint32_t cookie;
    memcpy(&cookie, data + 4, 4);
    cookie = ntohl(cookie);
    if (cookie != STUN_MAGIC_COOKIE) return;

    // ✅ 验证凭据
    std::string peerUfrag;
    if (!validateCredentials(data, len, peerUfrag)) {
        std::cerr << "[IceServer] Credential validation failed from "
                  << remote.address().to_string() << ":" << remote.port()
                  << std::endl;
        return;
    }

    static int okCount = 0;
    if (++okCount <= 5) {
        std::cout << "[IceServer] ✅ STUN OK peer=" << peerUfrag
                  << " from " << remote.address().to_string() << ":" << remote.port() << std::endl;
    }

    sendResponse(data, len, remote);
}

// ============================================================
// 凭据验证 (RFC 5389 §15.4)
//
// 算法:
//   1. 找到 USERNAME attribute → 提取 peer 的 ufrag
//   2. 定位 MESSAGE-INTEGRITY attribute
//   3. 把包中 MESSAGE-INTEGRITY 的 20 字节 HMAC 值清零
//   4. 调整消息头的 length 字段 = 到 MESSAGE-INTEGRITY 末尾的字节数
//   5. HMAC-SHA1(key = ice-pwd, data = 上面处理后的包)
//   6. 和收到的 MESSAGE-INTEGRITY 比较
// ============================================================
bool IceServer::validateCredentials(const uint8_t* data, size_t len,
                                     std::string& outPeerUfrag) {
    // ---- 遍历所有 attribute ----
    // attr: type(2) | len(2) | value(len) | padding(0~3)
    size_t pos = STUN_HEADER_SIZE;
    const uint8_t* miPos  = nullptr;  // MESSAGE-INTEGRITY 在包中的位置
    size_t miValPos = 0;              // HMAC 值在包中的偏移

    while (pos + 4 <= len) {
        uint16_t attrType, attrLen;
        memcpy(&attrType, data + pos,     2);
        memcpy(&attrLen,  data + pos + 2, 2);
        attrType = ntohs(attrType);
        attrLen  = ntohs(attrLen);

        size_t valEnd = pos + 4 + attrLen;
        if (valEnd > len) break;  // 截断

        if (attrType == ATTR_USERNAME) {
            // USERNAME 格式 (ICE): "server_ufrag:peer_ufrag"
            std::string username(reinterpret_cast<const char*>(data + pos + 4), attrLen);
            size_t colon = username.find(':');
            if (colon != std::string::npos) {
                std::string srvUfrag = username.substr(0, colon);
                outPeerUfrag = username.substr(colon + 1);
                if (srvUfrag != _iceUfrag) {
                    std::cerr << "[IceServer] ufrag mismatch: got '"
                              << srvUfrag << "' want '" << _iceUfrag << "'"
                              << std::endl;
                    return false;  // 不是发给这个 SFU 的
                }
            }
        }

        if (attrType == ATTR_MESSAGE_INTEGRITY) {
            if (attrLen != 20) return false;  // HMAC-SHA1 必须 20 字节
            miPos    = data + pos;            // MI attribute 起始地址
            miValPos = pos + 4;               // HMAC 值起始地址
        }

        // 前进到下一个 attribute（4 字节对齐）
        pos = (valEnd + 3) & ~3;
    }

    if (!miPos || outPeerUfrag.empty()) return false;

    // ---- 构造 HMAC 输入 ----
    // RFC 5389 §15.4: HMAC 计算时 MESSAGE-INTEGRITY 属性还不存在
    // 所以 HMAC 输入 = STUN 消息去掉整个 MESSAGE-INTEGRITY 属性（header+value）
    // length 字段保持调整后的值（包含 MI 属性大小）
    size_t miAttrStart = miPos - data;  // MI 属性起始偏移
    size_t hmacInputLen = miAttrStart;  // HMAC 输入 = MI 之前的所有内容

    static uint8_t buf[4096];
    if (hmacInputLen > sizeof(buf)) return false;
    memcpy(buf, data, hmacInputLen);

    // ① 修改消息头 length 字段 = 到 MI 属性末尾（包含 MI 但不包含 FINGERPRINT）
    //    miValPos + 20 = MI 属性末尾偏移
    uint16_t newLen = htons(static_cast<uint16_t>(miValPos + 20 - STUN_HEADER_SIZE));
    memcpy(buf + 2, &newLen, 2);

    // ② HMAC-SHA1(_icePwd, buf[0..hmacInputLen-1])
    unsigned int hmacLen = 20;
    uint8_t      hmac[20];
    HMAC(EVP_sha1(),
         _icePwd.data(), static_cast<int>(_icePwd.size()),
         buf, hmacInputLen,
         hmac, &hmacLen);

    // ③ 比较
    if (memcmp(hmac, data + miValPos, 20) != 0) {
        std::cerr << "[IceServer] MESSAGE-INTEGRITY mismatch" << std::endl;
        return false;
    }

    return true;
}

// ============================================================
// 构造 Binding Response + MESSAGE-INTEGRITY
//
// HMAC 计算：取 MESSAGE-INTEGRITY 属性之前的所有内容（不含 MI 属性本身）
// ============================================================
void IceServer::sendResponse(const uint8_t* request, size_t /*reqLen*/,
                             const boost::asio::ip::udp::endpoint& remote) {
    uint8_t response[256];
    size_t  respLen = 0;

    // --- 消息头 (20) ---
    uint16_t respType = htons(BINDING_RESPONSE);
    memcpy(response, &respType, 2);
    memset(response + 2, 0, 2);                               // length 稍后填写

    uint32_t magic = htonl(STUN_MAGIC_COOKIE);
    memcpy(response + 4, &magic, 4);
    memcpy(response + 8, request + 8, 12);                    // Transaction ID 原样复制
    respLen = STUN_HEADER_SIZE;

    // --- XOR-MAPPED-ADDRESS (8) ---
    {
        uint16_t type = htons(ATTR_XOR_MAPPED);
        uint16_t alen = htons(8);
        memcpy(response + respLen, &type, 2); respLen += 2;
        memcpy(response + respLen, &alen, 2); respLen += 2;

        response[respLen++] = 0;                              // reserved
        response[respLen++] = 0x01;                           // IPv4
        // RFC 5389 §15.2: X-Port = htons(port_host ^ (magic>>16)), X-Addr = htonl(ip_host ^ magic)
        uint16_t xorPort = htons(remote.port() ^ (STUN_MAGIC_COOKIE >> 16));
        uint32_t ipHost  = remote.address().to_v4().to_uint();
        uint32_t xorIp   = htonl(ipHost ^ STUN_MAGIC_COOKIE);
        memcpy(response + respLen, &xorPort, 2); respLen += 2;
        memcpy(response + respLen, &xorIp,   4); respLen += 4;
    }

    // --- 保存 MI 之前的长度（用于 HMAC 计算）---
    size_t preMiLen = respLen;  // HMAC 输入到此为止

    // --- 计算 HMAC（不含 MI 属性，length=到MI末尾不含FP）---
    {
        uint16_t lenForHmac = htons(static_cast<uint16_t>(respLen - STUN_HEADER_SIZE + 24));
        memcpy(response + 2, &lenForHmac, 2);

        uint8_t hmacVal[20];
        unsigned int hmacLen2 = 20;
        HMAC(EVP_sha1(),
             _icePwd.data(), static_cast<int>(_icePwd.size()),
             response, preMiLen,
             hmacVal, &hmacLen2);

        // --- 追加 MESSAGE-INTEGRITY (24) ---
        uint16_t type = htons(ATTR_MESSAGE_INTEGRITY);
        uint16_t alen = htons(20);
        memcpy(response + respLen, &type, 2); respLen += 2;
        memcpy(response + respLen, &alen, 2); respLen += 2;
        memcpy(response + respLen, hmacVal, 20); respLen += 20;
    }

    // --- 设为最终 length（含 FP），再算 FINGERPRINT ---
    {
        uint16_t finalLen = htons(static_cast<uint16_t>(respLen - STUN_HEADER_SIZE + 8));
        memcpy(response + 2, &finalLen, 2);

        uint32_t fpVal = crc32(0, response, respLen) ^ 0x5354554E;

        uint16_t type = htons(ATTR_FINGERPRINT);
        uint16_t alen = htons(4);
        memcpy(response + respLen, &type, 2); respLen += 2;
        memcpy(response + respLen, &alen, 2); respLen += 2;
        uint32_t fpNet = htonl(fpVal);
        memcpy(response + respLen, &fpNet, 4); respLen += 4;
    }

    if (_sendCb) {
        static int sendCount = 0;
        if (++sendCount <= 5) {
            std::cout << "[IceServer] 📤 Sent response to "
                      << remote.address().to_string() << ":" << remote.port() << std::endl;
        }
        _sendCb(response, respLen, remote);
    }
}
