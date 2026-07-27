#pragma once
#include "RouteTable.h"
#include "Consumer.h"
#include "Producer.h"
#include <memory>
#include <shared_mutex>
#include <mutex>
#include <random>
#include <unordered_map>
#include <map>
#include <vector>
#include <string>
#include <cstdint>

struct Peer;
class UdpServer;

// SFU 路由核心 - Consumer 模型
//   每个订阅关系是一个 Consumer（含独立出口 SSRC + seq/ts 重写状态）
//   每路发送流是一个 Producer（含订阅该流的 Consumer 列表）
// RTP 转发：重写 SSRC/seq/timestamp；RTCP：翻译 PLI/NACK/SR/RR
class Router {
public:
    Router(std::shared_ptr<UdpServer> udp,
           std::shared_ptr<RouteTable> table);

    // === 兼容旧接口（Task 8 后 MediaServiceImpl 不再调用，保留 no-op 以维持编译） ===
    void addForwarding(Peer* fromPeer, Peer* toPeer);

    // === Consumer 模型 ===
    // 为 subscriber 创建订阅 publisher 某路流的 Consumer，返回分配的出口 SSRC
    uint32_t addConsumer(Peer* subscriber, Peer* publisher, bool isVideo);

    // Peer 离开：删其所有 Producer + Consumer，解绑 SSRC
    void removePeer(Peer* peer);

    // === RTP 转发（SSRC/seq/ts 重写） ===
    void onRtpPacket(const uint8_t* plainRtp, size_t len, Peer* fromPeer);

    // === RTCP 翻译入口（PacketRouter 调用，Task 6 实现具体逻辑） ===
    void onRtcpPacket(const uint8_t* rtcp, size_t len, Peer* fromPeer);

    // === SendOffer 用：把 recvMid 绑到 subscriber 的对应 Consumer，返回出口 SSRC ===
    uint32_t bindConsumerByMid(Peer* subscriber,
                              const std::string& publisherPeerId,
                              bool isVideo,
                              const std::string& recvMid);

    // === SendOffer 用：按 recvMid 查出口 SSRC ===
    uint32_t findRewrittenSsrc(Peer* subscriber, const std::string& recvMid);

    // === SendOffer 用：查 subscriber 所有已绑定 recvMid 的 Consumer，返回 mid->ssrc 映射 ===
    // 每次 SendOffer 都调用，确保 answer 中所有 recvonly mid 都有 a=ssrc（即使本次没新绑 recv_mids）
    std::map<std::string, uint32_t> getAllBoundSsrcs(Peer* subscriber);

    // === PacketRouter::sendPLItoAllPeers 用：拿所有 video Producer ===
    std::vector<Producer*> getAllVideoProducers();

    // === 测试可见：重写 RTP 头（生产路径上也用） ===
    void rewriteRtpHeader(uint8_t* buf, size_t len, Consumer& c);

    // === 兼容旧接口：SSRC→Peer 查询（PacketRouter::forwardPLI 过渡用，Task 9 后移除） ===
    Peer* findPeerBySsrc(uint32_t ssrc);

private:
    std::shared_ptr<UdpServer>  _udp;
    std::shared_ptr<RouteTable> _table;

    // 单把锁保护所有 Router 可变状态：_allPeers、各 Peer 的 producers/consumers 向量、
    // producer->consumers 列表、_outSsrcToConsumer。9 人小规模，单锁足够且简单可靠。
    mutable std::shared_mutex _mutex;

    std::vector<Peer*> _allPeers;

    // SSRC 分配器
    std::mt19937 _ssrcGen{std::random_device{}()};
    std::mutex _ssrcGenMutex;
    uint32_t allocateSsrc();

    // 出口 SSRC → Consumer 反查（RTCP 翻译用）
    std::unordered_map<uint32_t, Consumer*> _outSsrcToConsumer;
    Consumer* findConsumerByOutSsrc(uint32_t outSsrc);

    // 纯读：按 SSRC 查已存在的 Producer（不创建）
    Producer* findProducer(Peer* fromPeer, uint32_t ssrc);
    // 读写：查不到则创建，并关联等待的 Consumer
    Producer* findOrCreateProducer(Peer* fromPeer, uint32_t ssrc, bool isVideo);
    void registerPeer(Peer* p);

    // === RTCP 翻译内部方法（Task 6 实现） ===
    void handlePLI(const uint8_t* rtcp, size_t len, Peer* subscriber);
    void handleNACK(const uint8_t* rtcp, size_t len, Peer* subscriber);
    void handleRR(const uint8_t* rtcp, size_t len, Peer* subscriber);
    void generateAndSendSR(Consumer* c);
};
