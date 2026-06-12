#include "RtcpHandler.h"
#include <cstring>
#include <arpa/inet.h>
#include <iostream>

// ============================================================
// RTCP 常量 (RFC 3550, RFC 4585, RFC 5104)
// ============================================================
static const uint8_t  RTCP_V2       = 0x80;   // V=2
static const uint8_t  PT_SR         = 200;
static const uint8_t  PT_RR         = 201;
static const uint8_t  PT_SDES       = 202;
static const uint8_t  PT_BYE        = 203;
static const uint8_t  PT_RTPFB      = 205;    // Transport-layer FB (NACK)
static const uint8_t  PT_PSFB       = 206;    // Payload-specific FB (PLI)
static const uint8_t  FMT_NACK      = 1;      // Generic NACK
static const uint8_t  FMT_PLI       = 1;      // Picture Loss Indication
static const uint8_t  SDES_CNAME    = 1;      // Canonical Name

// ============================================================
// 复合包解析 — 遍历每个子包
// ============================================================
void RtcpHandler::onRtcpPacket(const uint8_t* data, size_t len) {
    const uint8_t* p    = data;
    const uint8_t* end  = data + len;

    while (p + 4 <= end) {
        uint8_t  pt     = p[1];
        uint16_t length;   // 32-bit words - 1
        memcpy(&length, p + 2, 2);
        length = ntohs(length);

        size_t packetLen = (static_cast<size_t>(length) + 1) * 4;
        if (p + packetLen > end) {
            std::cerr << "[RtcpHandler] Truncated packet, pt="
                      << static_cast<int>(pt) << std::endl;
            break;
        }

        switch (pt) {
        case PT_SR:
            // SR: SSRC(4) + NTP(8) + RTP(4) + pktCnt(4) + octCnt(4) + reports
            // 暂存 sender SSRC, 用于后续 RTCP 发送
            break;

        case PT_SDES:
            // SDES: 提取 CNAME → SSRC 绑定
            {
                uint32_t ssrc;
                memcpy(&ssrc, p + 4, 4);
                ssrc = ntohl(ssrc);

                // 跳过 SSRC, 解析 items
                const uint8_t* item = p + 8;
                const uint8_t* itemEnd = p + packetLen;

                while (item + 2 <= itemEnd && item[0] != 0) {
                    uint8_t type   = item[0];
                    uint8_t valLen = item[1];
                    if (item + 2 + valLen > itemEnd) break;

                    if (type == SDES_CNAME && valLen > 0) {
                        std::string cname(reinterpret_cast<const char*>(item + 2), valLen);
                        if (_sdesCb) _sdesCb(ssrc, cname);
                    }

                    item += 2 + valLen;  // type(1) + len(1) + value
                }
            }
            break;

        case PT_RTPFB:
            // NACK (FMT=1)
            {
                uint8_t fmt = p[0] & 0x1F;
                if (fmt == FMT_NACK && packetLen >= 16) {
                    uint32_t mediaSsrc;
                    memcpy(&mediaSsrc, p + 8, 4);
                    mediaSsrc = ntohl(mediaSsrc);

                    uint16_t pid, blp;
                    memcpy(&pid, p + 12, 2);
                    memcpy(&blp, p + 14, 2);
                    pid = ntohs(pid);
                    blp = ntohs(blp);

                    if (_nackCb) _nackCb(mediaSsrc, pid, blp);
                }
            }
            break;

        case PT_PSFB:
            // PLI (FMT=1)
            {
                uint8_t fmt = p[0] & 0x1F;
                if (fmt == FMT_PLI && packetLen >= 12) {
                    uint32_t mediaSsrc;
                    memcpy(&mediaSsrc, p + 8, 4);
                    mediaSsrc = ntohl(mediaSsrc);

                    if (_pliCb) _pliCb(mediaSsrc);
                }
            }
            break;

        default:
            break;
        }

        p += packetLen;
    }
}

// ============================================================
// 构造 PLI (Picture Loss Indication)
//
// 结构 (RFC 4585 §6.1):
//    0                   1                   2                   3
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |V=2|P| FMT=1  | PT=PSFB=206  |         length=2              |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                    SSRC of packet sender                      |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                    SSRC of media source                       |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// ============================================================
void RtcpHandler::sendPLI(uint32_t senderSsrc, uint32_t mediaSsrc) {
    if (!_sendCb) return;

    uint8_t buf[12];
    buf[0] = RTCP_V2 | FMT_PLI;        // V=2, P=0, FMT=1
    buf[1] = PT_PSFB;                   // 206

    uint16_t len = htons(2);            // (12/4) - 1 = 2
    memcpy(buf + 2, &len, 2);

    uint32_t ssrc = htonl(senderSsrc);
    memcpy(buf + 4, &ssrc, 4);

    uint32_t mssrc = htonl(mediaSsrc);
    memcpy(buf + 8, &mssrc, 4);

    _sendCb(buf, sizeof(buf));
}

// ============================================================
// 构造 NACK (Generic NACK)
//
// 结构 (RFC 4585 §6.2.1):
//    0                   1                   2                   3
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |V=2|P| FMT=1  | PT=RTPFB=205  |         length                |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                    SSRC of packet sender                      |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                    SSRC of media source                       |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |            PID                |             BLP               |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// ============================================================
void RtcpHandler::sendNACK(uint32_t senderSsrc, uint32_t mediaSsrc,
                            uint16_t pid, uint16_t blp) {
    if (!_sendCb) return;

    uint8_t buf[16];
    buf[0] = RTCP_V2 | FMT_NACK;       // V=2, P=0, FMT=1
    buf[1] = PT_RTPFB;                  // 205

    uint16_t len = htons(3);            // (16/4) - 1 = 3
    memcpy(buf + 2, &len, 2);

    uint32_t ssrc = htonl(senderSsrc);
    memcpy(buf + 4, &ssrc, 4);

    uint32_t mssrc = htonl(mediaSsrc);
    memcpy(buf + 8, &mssrc, 4);

    uint16_t npid = htons(pid);
    memcpy(buf + 12, &npid, 2);

    uint16_t nblp = htons(blp);
    memcpy(buf + 14, &nblp, 2);

    _sendCb(buf, sizeof(buf));
}
