#include "RtpHeader.h"
#include <cstring>      // memcpy
#include <arpa/inet.h>  // ntohs, ntohl

bool RtpHeader::parse(const uint8_t* data, size_t len, RtpHeader& out) {
    if (len < 12) return false;

    // RTP 固定头 12 字节：
    //  0                   1                   2                   3
    //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |V=2|P|X|  CC   |M|     PT      |       sequence number         |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |                           timestamp                           |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |           synchronization source (SSRC) identifier            |
    // +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

    uint8_t  byte0, byte1;
    uint16_t seq, pt_ts;
    uint32_t ts, ssrc_val;

    memcpy(&byte0, data,      1);   // V+P+X+CC
    memcpy(&byte1, data + 1,  1);   // M+PT
    memcpy(&seq,   data + 2,  2);   // sequence number
    memcpy(&ts,    data + 4,  4);   // timestamp
    memcpy(&ssrc_val, data + 8, 4); // SSRC

    out.version        = (byte0 >> 6) & 0x03;
    out.padding        = (byte0 >> 5) & 0x01;
    out.extension      = (byte0 >> 4) & 0x01;
    out.csrcCount      = byte0 & 0x0F;
    out.marker         = (byte1 >> 7) & 0x01;
    out.payloadType    = byte1 & 0x7F;
    out.sequenceNumber = ntohs(seq);
    out.timestamp      = ntohl(ts);
    out.ssrc           = ntohl(ssrc_val);

    return true;
}

size_t RtpHeader::headerLength(const uint8_t* data, size_t len) {
    if (len < 12) return 0;

    size_t length = 12;                          // 固定头
    uint8_t cc = data[0] & 0x0F;
    length += cc * 4;                            // CSRC 列表

    bool extension = (data[0] >> 4) & 0x01;
    if (extension && len >= length + 4) {
        uint16_t extLen;
        memcpy(&extLen, data + length + 2, 2);   // extension header 的 length 字段
        length += 4 + ntohs(extLen) * 4;         // extension header + 数据
    }

    bool padding = (data[0] >> 5) & 0x01;
    if (padding && len > 0) {
        size_t padLen = data[len - 1];           // 最后一个字节是 padding 长度
        if (padLen > 0) length = len - padLen;    // 实际载荷 = 总长 - padding
    }

    return length;
}

void RtpHeader::writeFixedHeader(uint8_t* data, size_t len, const RtpHeader& hdr) {
    if (len < 12) return;

    uint8_t byte0 = ((hdr.version & 0x03) << 6)
                  | ((hdr.padding ? 1 : 0) << 5)
                  | ((hdr.extension ? 1 : 0) << 4)
                  | (hdr.csrcCount & 0x0F);
    uint8_t byte1 = ((hdr.marker ? 1 : 0) << 7) | (hdr.payloadType & 0x7F);

    uint16_t seq = htons(hdr.sequenceNumber);
    uint32_t ts  = htonl(hdr.timestamp);
    uint32_t sss = htonl(hdr.ssrc);

    memcpy(data,     &byte0, 1);
    memcpy(data + 1, &byte1, 1);
    memcpy(data + 2, &seq,   2);
    memcpy(data + 4, &ts,    4);
    memcpy(data + 8, &sss,   4);
}
