// SFU 转发功能测试 — 模拟两个 Peer 互相转发 RTP

#include "../sfu/Router.h"
#include "../sfu/Peer.h"
#include "../sfu/RouteTable.h"
#include "../transport/UdpServer.h"
#include "../transport/SrtpContext.h"
#include "../rtp/RtpHeader.h"

#include <boost/asio.hpp>
#include <iostream>
#include <cstring>
#include <cassert>

static int testsPassed = 0;
static int testsFailed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { testsPassed++; std::cout << "  ✅ " << msg << std::endl; } \
    else { testsFailed++; std::cerr << "  ❌ " << msg << std::endl; } \
} while(0)

static std::vector<uint8_t> makeRtp(uint32_t ssrc, uint16_t seq, uint32_t ts) {
    std::vector<uint8_t> pkt(12);
    pkt[0] = 0x80;
    pkt[1] = 96;
    pkt[2] = static_cast<uint8_t>(seq >> 8);
    pkt[3] = static_cast<uint8_t>(seq & 0xFF);
    pkt[4] = static_cast<uint8_t>(ts >> 24);
    pkt[5] = static_cast<uint8_t>((ts >> 16) & 0xFF);
    pkt[6] = static_cast<uint8_t>((ts >> 8) & 0xFF);
    pkt[7] = static_cast<uint8_t>(ts & 0xFF);
    pkt[8] = static_cast<uint8_t>(ssrc >> 24);
    pkt[9] = static_cast<uint8_t>((ssrc >> 16) & 0xFF);
    pkt[10] = static_cast<uint8_t>((ssrc >> 8) & 0xFF);
    pkt[11] = static_cast<uint8_t>(ssrc & 0xFF);
    pkt.resize(12 + 100, 0x00);
    return pkt;
}

static std::shared_ptr<SrtpContext> makeSrtp() {
    std::string keys(60, '\x00');
    for (int i = 0; i < 60; i++) keys[i] = static_cast<char>(i);

    auto ctx = std::make_shared<SrtpContext>();
    ctx->init(keys);
    return ctx;
}

int main() {
    std::cout << "=== SFU Forward Test ===" << std::endl;
    boost::asio::io_context ioc;

    auto udp = std::make_shared<UdpServer>(ioc, 20001);
    auto table = std::make_shared<RouteTable>();
    auto router = std::make_shared<Router>(udp, table);

    // 创建两个 Peer
    auto peerA = std::make_shared<Peer>();
    peerA->peerId = "peerA";
    peerA->srtp = makeSrtp();
    peerA->remoteEp = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), 30001);
    CHECK(peerA->srtp != nullptr, "PeerA SRTP created");

    auto peerB = std::make_shared<Peer>();
    peerB->peerId = "peerB";
    peerB->srtp = makeSrtp();
    peerB->remoteEp = boost::asio::ip::udp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), 30002);
    CHECK(peerB->srtp != nullptr, "PeerB SRTP created");

    // Go 侧逻辑: A 的流转发给 B, B 的流转发给 A
    router->addForwarding(peerA.get(), peerB.get());
    router->addForwarding(peerB.get(), peerA.get());

    CHECK(peerA->forwardTo.size() == 1, "A forwards to 1 peer");
    CHECK(peerB->forwardTo.size() == 1, "B forwards to 1 peer");
    CHECK(peerA->forwardTo[0] == peerB.get(), "A → B");
    CHECK(peerB->forwardTo[0] == peerA.get(), "B → A");

    // 模拟 RTP 转发
    auto rtpPkt = makeRtp(100, 1, 90000);
    std::cout << "\n--- Test: onRtpPacket from peerA" << std::endl;
    router->onRtpPacket(rtpPkt.data(), rtpPkt.size(), peerA.get());

    Peer* found = table->lookup(100);
    CHECK(found == peerA.get(), "SSRC 100 → PeerA");
    CHECK(peerA->forwardedPackets == 1, "A forwarded 1 packet");

    // 模拟重复添加（应去重）
    router->addForwarding(peerA.get(), peerB.get());
    CHECK(peerA->forwardTo.size() == 1, "Duplicate addForwarding ignored");

    // 模拟 Peer 离开
    router->removePeer(peerA.get());
    CHECK(peerA->forwardTo.empty(), "A forwardTo cleared on remove");

    std::cout << "\n=== Results: " << testsPassed << "/"
              << (testsPassed + testsFailed) << " passed ===" << std::endl;
    return (testsFailed > 0) ? 1 : 0;
}
