// media-svc/rtp/test_rtp_header.cpp
#include "RtpHeader.h"
#include <cassert>
#include <iostream>
#include <cstring>
#include <arpa/inet.h>

int main() {
    // 构造一个 RTP 包: PT=96, seq=1000, ts=90000, ssrc=0x12345678
    uint8_t pkt[12] = {0};
    pkt[0] = 0x80;            // V=2, P=0, X=0, CC=0
    pkt[1] = 96;              // M=0, PT=96
    uint16_t seq = htons(1000);    memcpy(pkt+2, &seq, 2);
    uint32_t ts  = htonl(90000);   memcpy(pkt+4, &ts,  4);
    uint32_t sss = htonl(0x12345678u); memcpy(pkt+8, &sss, 4);

    RtpHeader h;
    assert(RtpHeader::parse(pkt, 12, h));
    assert(h.sequenceNumber == 1000);
    assert(h.timestamp == 90000);
    assert(h.ssrc == 0x12345678u);
    assert(h.payloadType == 96);

    // 改写后写回
    h.ssrc = 0xAABBCCDDu;
    h.sequenceNumber = 2000;
    h.timestamp = 180000;
    RtpHeader::writeFixedHeader(pkt, 12, h);

    // 重新解析验证
    RtpHeader h2;
    assert(RtpHeader::parse(pkt, 12, h2));
    assert(h2.ssrc == 0xAABBCCDDu);
    assert(h2.sequenceNumber == 2000);
    assert(h2.timestamp == 180000);
    assert(h2.payloadType == 96);
    assert(h2.version == 2);

    std::cout << "✅ RtpHeader writeFixedHeader test passed\n";
    return 0;
}
