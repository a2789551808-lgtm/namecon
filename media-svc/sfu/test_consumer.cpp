// media-svc/sfu/test_consumer.cpp
// Consumer 模型单元测试：验证 addConsumer / bindConsumerByMid / rewriteRtpHeader / removePeer
#include "../sfu/Router.h"
#include "../sfu/Peer.h"
#include "../sfu/RouteTable.h"
#include "../transport/UdpServer.h"
#include "../transport/SrtpContext.h"
#include "../rtp/RtpHeader.h"
#include <boost/asio.hpp>
#include <iostream>
#include <cassert>
#include <cstring>
#include <arpa/inet.h>

static int pass=0, fail=0;
#define CK(c,m) do{ if(c){pass++; std::cout<<"  ✅ "<<m<<"\n";} else {fail++; std::cerr<<"  ❌ "<<m<<"\n";} }while(0)

static std::vector<uint8_t> makeRtp(uint32_t ssrc, uint16_t seq, uint32_t ts) {
    std::vector<uint8_t> p(12, 0);
    p[0]=0x80; p[1]=96;
    uint16_t s=htons(seq); memcpy(p.data()+2,&s,2);
    uint32_t t=htonl(ts);  memcpy(p.data()+4,&t,4);
    uint32_t ss=htonl(ssrc); memcpy(p.data()+8,&ss,4);
    return p;
}

int main() {
    boost::asio::io_context ioc;
    auto udp = std::make_shared<UdpServer>(ioc, 20010);
    auto table = std::make_shared<RouteTable>();
    auto router = std::make_shared<Router>(udp, table);

    auto A = std::make_shared<Peer>(); A->peerId = "A";
    auto B = std::make_shared<Peer>(); B->peerId = "B";

    // addConsumer：B 订阅 A 的视频
    uint32_t outSsrc = router->addConsumer(B.get(), A.get(), /*isVideo=*/true);
    CK(outSsrc != 0, "addConsumer returns nonzero SSRC");
    CK(outSsrc >= 0x40000000u && outSsrc <= 0x7FFFFFFFu, "SSRC in SFU range [0x40000000,0x7FFFFFFF]");
    CK(B->consumers.size() == 1, "B has 1 consumer");

    // bindConsumerByMid：绑定 mid="2"
    uint32_t bound = router->bindConsumerByMid(B.get(), "A", true, "2");
    CK(bound == outSsrc, "bindConsumerByMid returns same SSRC");

    Consumer* c = B->consumers[0].get();
    CK(c->recvMid == "2", "consumer recvMid bound");

    // 第一包：seq 不变（delta=0），SSRC 改写为出口
    auto pkt1 = makeRtp(1001, 500, 90000);
    router->rewriteRtpHeader(pkt1.data(), pkt1.size(), *c);
    RtpHeader h1; RtpHeader::parse(pkt1.data(), pkt1.size(), h1);
    CK(h1.ssrc == outSsrc, "1st pkt SSRC rewritten to outSsrc");
    CK(h1.sequenceNumber == 500, "1st pkt seq unchanged (delta 0)");

    // 第二包 seq+1：出口 seq 连续递增
    auto pkt2 = makeRtp(1001, 501, 90300);
    router->rewriteRtpHeader(pkt2.data(), pkt2.size(), *c);
    RtpHeader h2; RtpHeader::parse(pkt2.data(), pkt2.size(), h2);
    CK(h2.ssrc == outSsrc, "2nd pkt SSRC");
    CK(h2.sequenceNumber == 501, "2nd pkt seq continuous (501)");

    // getAllVideoProducers：尚未推流，应为空
    CK(router->getAllVideoProducers().empty(), "no producers until RTP arrives");

    // === PLI 翻译测试 ===
    // 浏览器 B 发 PLI，mediaSSRC = 出口 SSRC（outSsrc）
    // SFU 应翻译为 publisherSsrc 并发给 publisher（A）。这里只验证翻译函数的 SSRC 解析与构造。
    uint8_t pli[12];
    pli[0] = 0x81; pli[1] = 206;           // V=2,FMT=1,PT=PSFB
    uint16_t pliLen = htons(2); memcpy(pli+2, &pliLen, 2);
    uint32_t zero = 0; memcpy(pli+4, &zero, 4);
    uint32_t med = htonl(outSsrc); memcpy(pli+8, &med, 4);  // mediaSSRC = 出口

    // 调用 onRtcpPacket（不会崩，A 无 srtp 时静默跳过）
    router->onRtcpPacket(pli, 12, B.get());
    CK(true, "onRtcpPacket(PLI) does not crash");

    // NACK 翻译：构造一条 NACK，pid=500（出口空间），应翻译回原空间
    uint8_t nack[16];
    nack[0]=0x81; nack[1]=205;
    uint16_t nl=htons(3); memcpy(nack+2,&nl,2);
    uint32_t z2=0; memcpy(nack+4,&z2,4);
    uint32_t med2=htonl(outSsrc); memcpy(nack+8,&med2,4);
    uint16_t pid=htons(500), blp=htons(0);
    memcpy(nack+12,&pid,2); memcpy(nack+14,&blp,2);
    router->onRtcpPacket(nack, 16, B.get());
    CK(true, "onRtcpPacket(NACK) does not crash");

    // removePeer 不崩溃，且清空其 consumers
    router->removePeer(B.get());
    CK(B->consumers.empty(), "B consumers cleared after removePeer");

    std::cout << "\n=== " << pass << "/" << (pass+fail) << " ===\n";
    return fail ? 1 : 0;
}
