#include "IceServer.h"
#include <cstring>
#include <ctime>
#include <random>
#include <arpa/inet.h>
#include <openssl/hmac.h>
#include <iostream>

// STUN 消息头 (20 字节)
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |         Message Type          |         Message Length        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                         Magic Cookie                          |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                                                               |
// |                     Transaction ID (96 bits)                  |
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

static const uint32_t STUN_MAGIC_COOKIE      = 0x2112A442;
static const uint16_t BINDING_REQUEST        = 0x0001;
static const uint16_t BINDING_RESPONSE       = 0x0101;
static const uint16_t ATTR_XOR_MAPPED        = 0x0020;
static const uint16_t ATTR_USERNAME          = 0x0006;
static const uint16_t ATTR_MESSAGE_INTEGRITY = 0x0008;
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

    // ✅ 验证凭据：USERNAME 含 ufrag + MESSAGE-INTEGRITY 校验 pwd
    std::string peerUfrag;
    if (!validateCredentials(data, len, peerUfrag)) {
        std::cerr << "[IceServer] Credential validation failed from "
                  << remote.address().to_string() << ":" << remote.port()
                  << std::endl;
        return;  // 不回复 = 告诉浏览器 "你发错了"
    }

    std::cout << "[IceServer] Validated peer ufrag=" << peerUfrag
              << " from " << remote.address().to_string()
              << ":" << remote.port() << std::endl;

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
    // 复制包，把 length 字段改为到 MI 末尾的长度，把 MI 的 20 字节 value 清零
    size_t msgEnd = miValPos + 20;  // 到 HMAC 值末尾
    if (msgEnd > len) return false;

    static uint8_t buf[4096];
    size_t copyLen = msgEnd;
    if (copyLen > sizeof(buf)) return false;
    memcpy(buf, data, copyLen);

    // ① 修改消息头 length 字段 (bytes 2-3)
    uint16_t newLen = htons(static_cast<uint16_t>(msgEnd - STUN_HEADER_SIZE));
    memcpy(buf + 2, &newLen, 2);

    // ② 清零 HMAC 值
    memset(buf + miValPos, 0, 20);

    // ③ HMAC-SHA1(_icePwd, buf[0..msgEnd-1])
    unsigned int hmacLen = 20;
    uint8_t      hmac[20];
    HMAC(EVP_sha1(),
         _icePwd.data(), static_cast<int>(_icePwd.size()),
         buf, msgEnd,
         hmac, &hmacLen);

    // ④ 比较
    if (memcmp(hmac, data + miValPos, 20) != 0) {
        std::cerr << "[IceServer] MESSAGE-INTEGRITY mismatch" << std::endl;
        return false;
    }

    return true;
}

// ============================================================
// 构造 Binding Response + MESSAGE-INTEGRITY
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
        uint16_t xorPort = htons(remote.port()) ^ (STUN_MAGIC_COOKIE >> 16);
        uint32_t ipRaw   = htonl(remote.address().to_v4().to_uint());
        uint32_t xorIp   = ipRaw ^ STUN_MAGIC_COOKIE;
        memcpy(response + respLen, &xorPort, 2); respLen += 2;
        memcpy(response + respLen, &xorIp,   4); respLen += 4;
    }

    // --- MESSAGE-INTEGRITY (24): type(2) + len(2) + hmac(20) ---
    {
        uint16_t type = htons(ATTR_MESSAGE_INTEGRITY);
        uint16_t alen = htons(20);
        memcpy(response + respLen, &type, 2); respLen += 2;
        memcpy(response + respLen, &alen, 2); respLen += 2;

        // 先填 0，算 HMAC 后再覆盖
        size_t hmacPos = respLen;
        memset(response + respLen, 0, 20);
        respLen += 20;

        // 回填消息头 length（到 MI 末尾）
        uint16_t msgLen = htons(static_cast<uint16_t>(respLen - STUN_HEADER_SIZE));
        memcpy(response + 2, &msgLen, 2);

        // HMAC-SHA1(_icePwd, response[0..respLen-1])
        unsigned int hmacLen2 = 20;
        HMAC(EVP_sha1(),
             _icePwd.data(), static_cast<int>(_icePwd.size()),
             response, respLen,
             response + hmacPos, &hmacLen2);
    }

    if (_sendCb) {
        _sendCb(response, respLen, remote);
    }
}
