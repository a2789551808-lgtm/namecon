// media-svc/sfu/Consumer.h
#pragma once
#include <cstdint>
#include <string>

struct Peer;

// 一个 Consumer = 一个订阅者接收一路流
// 替代旧的 Peer::forwardTo（peer 级），改为流级粒度
// 由 Router::addConsumer 创建，承载 SSRC/seq/timestamp 重写状态与 SR 统计
struct Consumer {
    // === 身份 ===
    std::string consumerId;       // 可选 UUID，用于 gRPC 引用
    Peer* subscriber = nullptr;  // 订阅者（接收方）
    Peer* publisher = nullptr;    // 发布者（发送方；首次 RTP 到达时绑定 publisherSsrc）
    bool isVideo = false;         // true=video, false=audio

    // === SSRC 改写 ===
    uint32_t rewrittenSsrc = 0;   // SFU 为该订阅者分配的出口 SSRC（写入 SDP answer 的 a=ssrc）
    uint32_t publisherSsrc = 0;   // 发送方原始 SSRC（首次 RTP 到达时记录）

    // === mid 绑定 ===
    std::string recvMid;          // 订阅者 SDP 里的 mid（SendOffer 时填充，非空才允许转发）

    // === seq/timestamp 重写状态（维护"原始空间→出口空间"的线性映射） ===
    bool     seqInitialized = false;
    uint16_t lastSentSeq = 0;       // 上次转发的出口 seq
    uint32_t lastSentTimestamp = 0; // 上次转发的出口 timestamp
    int32_t  seqDelta = 0;          // 原 seq → 出口 seq 的累计偏移（NACK 翻译用）
    int32_t  tsDelta = 0;           // 原 ts → 出口 ts 的累计偏移

    // === RTCP SR 重新生成的统计（SFU 作为该流的发送方视角） ===
    uint32_t packetsSent = 0;     // 该 Consumer 已转发的包数
    uint32_t octetsSent  = 0;     // 该 Consumer 已转发的字节数
    uint32_t lastSrNtpTs = 0;     // 上次发 SR 的 NTP 时间戳（中间 32 位）
    uint32_t lastSrRtpTs = 0;     // 上次发 SR 对应的 RTP 时间戳

    bool active = true;          // false=暂停转发（订阅者离开/inactive）
};
