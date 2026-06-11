#include "IceServer.h"
#include <cstring>
#include <ctime>
#include <random>
#include <arpa/inet.h>

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

static const uint32_t STUN_MAGIC_COOKIE = 0x2112A442;
static const uint16_t BINDING_REQUEST   = 0x0001;
static const uint16_t BINDING_RESPONSE  = 0x0101;
static const uint16_t ATTR_XOR_MAPPED   = 0x0020;
static const uint16_t ATTR_MESSAGE_INTEGRITY = 0x0008;
static const size_t   STUN_HEADER_SIZE  = 20;

// 随机字符串 — 只需唯一性，不是加密密钥，时间种子够用
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
}

void IceServer::setSendCallback(SendCallback cb) {
    _sendCb = std::move(cb);
}

void IceServer::onStunPacket(const uint8_t* data, size_t len,
                             const boost::asio::ip::udp::endpoint& remote) {
    if (len < STUN_HEADER_SIZE) return;

    // ① 读消息类型
    uint16_t msgType;
    memcpy(&msgType, data, 2);
    msgType = ntohs(msgType);

    // ② 只处理 Binding Request
    if (msgType != BINDING_REQUEST) return;

    // ③ 读 Magic Cookie + Transaction ID
    uint32_t cookie;
    memcpy(&cookie, data + 4, 4);
    cookie = ntohl(cookie);
    if (cookie != STUN_MAGIC_COOKIE) return;

    // ④ 构造 Binding Response
    //    响应 = 头(20) + XOR-MAPPED-ADDRESS(8) + 可选 MESSAGE-INTEGRITY(24)
    uint8_t response[256];
    size_t  respLen = 0;

    // --- 头 ---
    uint16_t respType = htons(BINDING_RESPONSE);
    memcpy(response, &respType, 2);          // Type = Binding Response

    // 先留 Message Length，等计算完再填
    memset(response + 2, 0, 2);

    uint32_t magic = htonl(STUN_MAGIC_COOKIE);
    memcpy(response + 4, &magic, 4);         // Magic Cookie

    memcpy(response + 8, data + 8, 12);      // Transaction ID（原样复制）
    respLen = STUN_HEADER_SIZE;

    // --- XOR-MAPPED-ADDRESS ---
    uint16_t attrType = htons(ATTR_XOR_MAPPED);
    uint16_t attrLen  = htons(8);            // IPv4 = 8 bytes
    memcpy(response + respLen, &attrType, 2); respLen += 2;
    memcpy(response + respLen, &attrLen,  2); respLen += 2;

    uint8_t  zero     = 0;
    uint8_t  family   = 0x01;                // IPv4
    uint16_t xorPort  = htons(remote.port()) ^ (STUN_MAGIC_COOKIE >> 16);
    uint32_t ipRaw    = htonl(remote.address().to_v4().to_uint());
    uint32_t xorIp    = ipRaw ^ STUN_MAGIC_COOKIE;

    response[respLen++] = zero;
    response[respLen++] = family;
    memcpy(response + respLen, &xorPort, 2); respLen += 2;
    memcpy(response + respLen, &xorIp,   4); respLen += 4;

    // ⑤ 回填 Message Length
    uint16_t msgLen = htons(respLen - STUN_HEADER_SIZE);
    memcpy(response + 2, &msgLen, 2);

    // ⑥ 发送
    if (_sendCb) {
        _sendCb(response, respLen, remote);
    }
}
