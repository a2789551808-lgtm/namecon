#include "Router.h"
#include "Peer.h"
#include "../transport/UdpServer.h"
#include "../transport/SrtpContext.h"
#include "../rtp/RtpHeader.h"
#include "../sdp/SdpParser.h"
#include "../utils/Logger.h"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <ctime>

Router::Router(std::shared_ptr<UdpServer> udp,
               std::shared_ptr<RouteTable> table)
    : _udp(std::move(udp))
    , _table(std::move(table))
{
}

// ============================================================
// 兼容旧接口：no-op。Consumer 模型下转发由 addConsumer 建立。
// ============================================================
void Router::addForwarding(Peer* fromPeer, Peer* toPeer) {
    (void)fromPeer; (void)toPeer;
}

Peer* Router::findPeerBySsrc(uint32_t ssrc) {
    return _table->lookup(ssrc);
}

// ============================================================
// SSRC 分配：范围 0x40000000~0x7FFFFFFF，避开浏览器随机 SSRC
// ============================================================
uint32_t Router::allocateSsrc() {
    std::lock_guard<std::mutex> lock(_ssrcGenMutex);
    std::uniform_int_distribution<uint32_t> dis(0x40000000u, 0x7FFFFFFFu);
    uint32_t ssrc;
    do { ssrc = dis(_ssrcGen); } while (ssrc == 0);
    return ssrc;
}

void Router::registerPeer(Peer* p) {
    // 调用方已持 _mutex 或在不需要锁的初始化路径；这里只做去重插入
    for (auto* e : _allPeers) if (e == p) return;
    _allPeers.push_back(p);
}

// ============================================================
// addConsumer：为 subscriber 创建订阅 publisher 某路流的 Consumer
// 返回分配的出口 SSRC（写入 SDP answer 的 a=ssrc）
// publisherSsrc 此时未知（发送方还没推流），在 findOrCreateProducer 时回填
// ============================================================
uint32_t Router::addConsumer(Peer* subscriber, Peer* publisher, bool isVideo) {
    auto c = std::make_unique<Consumer>();
    c->subscriber = subscriber;
    c->publisher = publisher;
    c->isVideo = isVideo;
    c->rewrittenSsrc = allocateSsrc();
    Consumer* raw = c.get();

    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        registerPeer(subscriber);
        registerPeer(publisher);
        _outSsrcToConsumer[raw->rewrittenSsrc] = raw;

        // 若 publisher 已推流（Producer 已存在），立即关联并回填 publisherSsrc。
        // 否则 publisherSsrc 留 0，等 findOrCreateProducer 首次见到该 SSRC 时关联。
        // 这解决"publisher 先推流、subscriber 后加入"的时序问题：
        //   findOrCreateProducer 只在创建新 Producer 时关联 Consumer，
        //   若 Producer 已存在则不会遍历关联，必须在此补上。
        for (auto& p : publisher->producers) {
            if (p->isVideo == isVideo && p->originalSsrc != 0) {
                raw->publisherSsrc = p->originalSsrc;
                p->consumers.push_back(raw);
                break;
            }
        }

        subscriber->consumers.push_back(std::move(c));
    }

    LOG_INFO("AddConsumer: {} <- {} ({}) outSsrc={}",
             subscriber->peerId, publisher->peerId,
             isVideo ? "video" : "audio", raw->rewrittenSsrc);
    return raw->rewrittenSsrc;
}

// ============================================================
// removePeer：删除该 Peer 的所有 Producer（作为发送方）和 Consumer（作为订阅方）
// 并从其他 Peer 的 Producer.consumers 里摘除指向它的 Consumer
// ============================================================
void Router::removePeer(Peer* peer) {
    std::unique_lock<std::shared_mutex> lock(_mutex);

    // ① 作为发送方：清空其 Producer，并把订阅它们的 Consumer 标记失效
    for (auto& p : peer->producers) {
        for (auto* c : p->consumers) {
            c->publisher = nullptr;
            c->active = false;
            _outSsrcToConsumer.erase(c->rewrittenSsrc);
        }
    }
    peer->producers.clear();

    // ② 作为订阅方：清空其 Consumer，并从对应 Producer.consumers 摘除
    for (auto& c : peer->consumers) {
        if (c->publisher) {
            for (auto& p : c->publisher->producers) {
                if (p->originalSsrc == c->publisherSsrc && p->originalSsrc != 0) {
                    auto& cs = p->consumers;
                    cs.erase(std::remove(cs.begin(), cs.end(), c.get()), cs.end());
                    break;
                }
            }
        }
        _outSsrcToConsumer.erase(c->rewrittenSsrc);
    }
    peer->consumers.clear();

    // ③ 从 _allPeers 摘除 + RouteTable 解绑
    _allPeers.erase(std::remove(_allPeers.begin(), _allPeers.end(), peer),
                    _allPeers.end());
    _table->unbindPeer(peer);
    LOG_INFO("Peer removed: {}", peer->peerId);
}

// ============================================================
// findProducer：纯读，按 SSRC 查已存在的 Producer（不创建）
// ============================================================
Producer* Router::findProducer(Peer* fromPeer, uint32_t ssrc) {
    for (auto& p : fromPeer->producers) {
        if (p->originalSsrc == ssrc) return p.get();
    }
    return nullptr;
}

// ============================================================
// findOrCreateProducer：首次见到某 SSRC 时创建 Producer，
// 并把所有等待该 (publisher, kind) 的 Consumer 关联上来（回填 publisherSsrc）
// ============================================================
Producer* Router::findOrCreateProducer(Peer* fromPeer, uint32_t ssrc, bool isVideo) {
    for (auto& p : fromPeer->producers) {
        if (p->originalSsrc == ssrc) return p.get();
    }

    auto producer = std::make_unique<Producer>();
    producer->publisher = fromPeer;
    producer->originalSsrc = ssrc;
    producer->isVideo = isVideo;
    Producer* raw = producer.get();
    fromPeer->producers.push_back(std::move(producer));

    _table->bind(ssrc, fromPeer);

    LOG_INFO("Producer created: {} ssrc={} isVideo={}", fromPeer->peerId, ssrc, isVideo);

    // video Producer 创建时立即向 publisher 发 PLI 请求关键帧。
    // 场景：B 开摄像头后首包到达 SFU，此时 A 早已在等收 B 的视频。
    // 若不主动请求 I 帧，A 只收到 P 帧无法解码，要等 A 自己发 PLI（有延迟）。
    if (isVideo && fromPeer->srtp) {
        uint8_t pli[12];
        pli[0] = 0x81; pli[1] = 206;
        uint16_t pliLen = htons(2);
        memcpy(pli + 2, &pliLen, 2);
        uint32_t senderSsrc = 0;
        memcpy(pli + 4, &senderSsrc, 4);
        uint32_t mediaSsrc = htonl(ssrc);
        memcpy(pli + 8, &mediaSsrc, 4);

        uint8_t out[64];
        memcpy(out, pli, 12);
        int outLen = 12;
        if (fromPeer->srtp->protectRtcp(out, &outLen)) {
            _udp->sendTo(out, static_cast<size_t>(outLen), fromPeer->remoteEp);
            LOG_INFO("Sent PLI to {} on new video Producer (ssrc={})", fromPeer->peerId, ssrc);
        }
    }

    // 关联所有等待该 (publisher, kind) 的 Consumer
    for (auto* sub : _allPeers) {
        for (auto& c : sub->consumers) {
            if (c->publisher == fromPeer && c->isVideo == isVideo && c->publisherSsrc == 0) {
                c->publisherSsrc = ssrc;
                raw->consumers.push_back(c.get());
            }
        }
    }
    return raw;
}

// ============================================================
// rewriteRtpHeader：重写 SSRC + 维护 seq/timestamp 线性映射
//   首包：以原始值为起点，偏移 0
//   后续：出口值 = 上次出口值 + 原始增量（保证出口视角连续）
// ============================================================
void Router::rewriteRtpHeader(uint8_t* buf, size_t len, Consumer& c) {
    RtpHeader hdr;
    if (!RtpHeader::parse(buf, len, hdr)) return;

    hdr.ssrc = c.rewrittenSsrc;

    if (!c.seqInitialized) {
        c.lastSentSeq = hdr.sequenceNumber;
        c.lastSentTimestamp = hdr.timestamp;
        c.seqDelta = 0;
        c.tsDelta = 0;
        c.seqInitialized = true;
    } else {
        uint16_t origSeqDelta = hdr.sequenceNumber - c.lastSentSeq;   // uint16 自动回绕
        uint32_t origTsDelta  = hdr.timestamp - c.lastSentTimestamp;  // uint32 自动回绕
        uint16_t newSentSeq = c.lastSentSeq + origSeqDelta;
        uint32_t newSentTs  = c.lastSentTimestamp + origTsDelta;
        c.seqDelta = static_cast<int32_t>(newSentSeq) - static_cast<int32_t>(hdr.sequenceNumber);
        c.tsDelta  = static_cast<int32_t>(newSentTs)  - static_cast<int32_t>(hdr.timestamp);
        c.lastSentSeq = newSentSeq;
        c.lastSentTimestamp = newSentTs;
        hdr.sequenceNumber = newSentSeq;
        hdr.timestamp = newSentTs;
    }
    RtpHeader::writeFixedHeader(buf, len, hdr);
}

// ============================================================
// onRtpPacket：RTP 转发核心
//   解析 → 找/建 Producer → 拷贝 Consumer 快照 → 对每个 Consumer 改写+加密+发送
// ============================================================
void Router::onRtpPacket(const uint8_t* plainRtp, size_t len, Peer* fromPeer) {
    RtpHeader hdr;
    if (!RtpHeader::parse(plainRtp, len, hdr)) return;

    bool isVideo = (SdpParser::videoPTs.count(hdr.payloadType) > 0);

    Producer* producer = nullptr;
    std::vector<Consumer*> targets;

    // 快速路径：读锁查已存在的 Producer（绝大多数 RTP 包走这里，高并发友好）
    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        producer = findProducer(fromPeer, hdr.ssrc);
        if (producer) {
            targets = producer->consumers;  // 快照
        }
    }

    // 慢路径：首次见到该 SSRC，写锁创建 Producer + 关联等待的 Consumer
    if (!producer) {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        // double-check：拿写锁前可能已有别的线程创建了
        producer = findProducer(fromPeer, hdr.ssrc);
        if (!producer) {
            producer = findOrCreateProducer(fromPeer, hdr.ssrc, isVideo);
        }
        if (producer) targets = producer->consumers;
    }

    if (targets.empty()) return;

    for (auto* c : targets) {
        if (!c->active) continue;
        if (!c->subscriber || !c->subscriber->srtp) continue;
        if (c->recvMid.empty()) continue;  // 还没协商好，跳过（等 SendOffer 绑定后开始转发）

        uint8_t out[65536];
        memcpy(out, plainRtp, len);
        rewriteRtpHeader(out, len, *c);

        int outLen = static_cast<int>(len);
        if (c->subscriber->srtp->protect(out, &outLen)) {
            _udp->sendTo(out, static_cast<size_t>(outLen), c->subscriber->remoteEp);
            c->packetsSent++;
            c->octetsSent += static_cast<uint32_t>(len);
        }
    }
}

uint32_t Router::bindConsumerByMid(Peer* subscriber,
                                   const std::string& publisherPeerId,
                                   bool isVideo,
                                   const std::string& recvMid) {
    // 写操作（修改 c->recvMid），必须用写锁
    std::unique_lock<std::shared_mutex> lock(_mutex);
    for (auto& c : subscriber->consumers) {
        if (c->isVideo == isVideo && c->publisher && c->publisher->peerId == publisherPeerId) {
            c->recvMid = recvMid;
            LOG_INFO("bindConsumerByMid: {} mid={} -> outSsrc={}",
                     subscriber->peerId, recvMid, c->rewrittenSsrc);
            return c->rewrittenSsrc;
        }
    }
    LOG_WARN("bindConsumerByMid: no consumer for {} <- {} ({})",
             subscriber->peerId, publisherPeerId,
             isVideo ? "video" : "audio");
    return 0;
}

uint32_t Router::findRewrittenSsrc(Peer* subscriber, const std::string& recvMid) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    for (auto& c : subscriber->consumers) {
        if (c->recvMid == recvMid) return c->rewrittenSsrc;
    }
    return 0;
}

std::map<std::string, uint32_t> Router::getAllBoundSsrcs(Peer* subscriber) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::map<std::string, uint32_t> result;
    for (auto& c : subscriber->consumers) {
        if (!c->recvMid.empty()) {
            result[c->recvMid] = c->rewrittenSsrc;
        }
    }
    return result;
}

std::vector<Producer*> Router::getAllVideoProducers() {
    std::vector<Producer*> out;
    std::shared_lock<std::shared_mutex> lock(_mutex);
    for (auto* p : _allPeers) {
        for (auto& pr : p->producers) {
            if (pr->isVideo) out.push_back(pr.get());
        }
    }
    return out;
}

Consumer* Router::findConsumerByOutSsrc(uint32_t outSsrc) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    auto it = _outSsrcToConsumer.find(outSsrc);
    return (it != _outSsrcToConsumer.end()) ? it->second : nullptr;
}

// ============================================================
// RTCP 包类型判定（内联，避免依赖 RtcpHandler 实例）
//   PLI:  byte[1]==206 && (byte[0]&0x1F)==1
//   NACK: byte[1]==205 && (byte[0]&0x1F)==1
//   SR:   byte[1]==200
//   RR:   byte[1]==201
// ============================================================
static bool rtcpIsPLI(const uint8_t* p, size_t len) {
    return len >= 12 && p[1] == 206 && (p[0] & 0x1F) == 1;
}
static bool rtcpIsNACK(const uint8_t* p, size_t len) {
    return len >= 16 && p[1] == 205 && (p[0] & 0x1F) == 1;
}
static bool rtcpIsSR(const uint8_t* p, size_t len)  { return len >= 28 && p[1] == 200; }
static bool rtcpIsRR(const uint8_t* p, size_t len)  { return len >= 8  && p[1] == 201; }

// NTP 时间戳（RFC 3550）：秒(32) + 分数(32)，从 1900-01-01 起算
static uint64_t getNtpTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto dur = now.time_since_epoch();
    int64_t secs = std::chrono::duration_cast<std::chrono::seconds>(dur).count();
    int64_t frac = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count() % 1000000000;
    // Unix epoch (1970) → NTP epoch (1900) 偏移 2208988800 秒
    uint64_t ntpSecs = static_cast<uint64_t>(secs) + 2208988800ULL;
    uint64_t ntpFrac = (static_cast<uint64_t>(frac) << 32) / 1000000000ULL;
    return (ntpSecs << 32) | ntpFrac;
}

// ============================================================
// onRtcpPacket：RTCP 翻译统一入口
//   PLI/NACK/RR：fromPeer 是订阅者，翻译 SSRC 后转发给 publisher
//   SR：fromPeer 是 publisher，丢弃（SFU 用 Consumer 统计自行重新生成 SR）
// ============================================================
void Router::onRtcpPacket(const uint8_t* rtcp, size_t len, Peer* fromPeer) {
    // RTCP compound packet: traverse each sub-packet
    size_t offset = 0;
    while (offset + 4 <= len) {
        const uint8_t* p = rtcp + offset;
        uint16_t words = (static_cast<uint16_t>(p[2]) << 8) | p[3];
        size_t pktLen = (static_cast<size_t>(words) + 1) * 4;
        if (pktLen == 0 || offset + pktLen > len) break;

        size_t remaining = len - offset;
        if (rtcpIsPLI(p, remaining)) {
            LOG_INFO("RTCP PLI from {}", fromPeer->peerId);
            handlePLI(p, pktLen, fromPeer);
        } else if (rtcpIsNACK(p, remaining)) {
            handleNACK(p, pktLen, fromPeer);
        } else if (rtcpIsRR(p, remaining)) {
            handleRR(p, pktLen, fromPeer);
        } else if (rtcpIsSR(p, remaining)) {
            /* publisher->SFU SR, drop */
        }
        offset += pktLen;
    }
}

// ============================================================
// PLI 翻译：mediaSSRC 从"出口"翻译为"发送方原始"，转发给 publisher
// ============================================================
void Router::handlePLI(const uint8_t* rtcp, size_t len, Peer* subscriber) {
    (void)len;
    uint32_t outSsrc;
    memcpy(&outSsrc, rtcp + 8, 4);
    outSsrc = ntohl(outSsrc);

    Consumer* c = findConsumerByOutSsrc(outSsrc);
    if (!c || !c->publisher || !c->publisher->srtp) return;
    Peer* publisher = c->publisher;

    uint8_t pli[12];
    pli[0] = 0x81; pli[1] = 206;
    uint16_t l = htons(2); memcpy(pli+2, &l, 2);
    uint32_t ss = 0; memcpy(pli+4, &ss, 4);                 // SFU senderSSRC=0
    uint32_t med = htonl(c->publisherSsrc); memcpy(pli+8, &med, 4);  // 翻译为原始 SSRC

    uint8_t out[64]; memcpy(out, pli, 12); int outLen = 12;
    if (publisher->srtp->protectRtcp(out, &outLen)) {
        _udp->sendTo(out, static_cast<size_t>(outLen), publisher->remoteEp);
    }
}

// ============================================================
// NACK 翻译：PID 从"出口 seq 空间"翻译回"原始 seq 空间"
//   出口 seq = 原 seq + seqDelta  →  原 seq = 出口 seq - seqDelta
//   BLP 是相对 PID 的位掩码，与 SSRC 无关，非 simulcast 下不变
// ============================================================
void Router::handleNACK(const uint8_t* rtcp, size_t len, Peer* subscriber) {
    (void)len; (void)subscriber;
    uint32_t outSsrc;
    memcpy(&outSsrc, rtcp + 8, 4);
    outSsrc = ntohl(outSsrc);

    uint16_t pid, blp;
    memcpy(&pid, rtcp + 12, 2); pid = ntohs(pid);
    memcpy(&blp, rtcp + 14, 2); blp = ntohs(blp);

    Consumer* c = findConsumerByOutSsrc(outSsrc);
    if (!c || !c->publisher || !c->publisher->srtp) return;
    Peer* publisher = c->publisher;

    uint16_t origPid = pid - static_cast<uint16_t>(c->seqDelta);
    uint16_t origBlp = blp;

    uint8_t nack[16];
    nack[0] = 0x81; nack[1] = 205;
    uint16_t l = htons(3); memcpy(nack+2, &l, 2);
    uint32_t ss = 0; memcpy(nack+4, &ss, 4);
    uint32_t med = htonl(c->publisherSsrc); memcpy(nack+8, &med, 4);
    uint16_t p = htons(origPid); memcpy(nack+12, &p, 2);
    uint16_t b = htons(origBlp); memcpy(nack+14, &b, 2);

    uint8_t out[64]; memcpy(out, nack, 16); int outLen = 16;
    if (publisher->srtp->protectRtcp(out, &outLen)) {
        _udp->sendTo(out, static_cast<size_t>(outLen), publisher->remoteEp);
    }
}

// ============================================================
// RR 翻译：reportee SSRC 从"出口"翻译为"原始"，其余字段透传
// ============================================================
void Router::handleRR(const uint8_t* rtcp, size_t len, Peer* subscriber) {
    (void)subscriber;
    if (len < 8) return;
    uint32_t outSsrc;
    memcpy(&outSsrc, rtcp + 4, 4);  // RR header 的 sender SSRC（这里取 report block 前的 SSRC）
    outSsrc = ntohl(outSsrc);

    Consumer* c = findConsumerByOutSsrc(outSsrc);
    if (!c || !c->publisher || !c->publisher->srtp) return;
    Peer* publisher = c->publisher;

    // 拷贝原 RR，把 reportee SSRC 替换为 publisherSsrc（简化：只替换第一个 report block 的 SSRC）
    std::vector<uint8_t> out(rtcp, rtcp + len);
    if (out.size() >= 12) {
        uint32_t med = htonl(c->publisherSsrc);
        memcpy(out.data() + 8, &med, 4);  // 第一个 report block 的 SSRC
    }
    int outLen = static_cast<int>(out.size());
    if (publisher->srtp->protectRtcp(out.data(), &outLen)) {
        _udp->sendTo(out.data(), static_cast<size_t>(outLen), publisher->remoteEp);
    }
}

// ============================================================
// SR 重新生成：SFU 作为该流的"发送方"向订阅者发 SR
//   sender SSRC = 出口 SSRC（订阅者视角的发送方 = SFU）
//   pktCnt/octCnt = 该 Consumer 的转发统计
// 触发：由外部定时器调用（Task 15 部署后可加定时器；此处提供方法）
// ============================================================
void Router::generateAndSendSR(Consumer* c) {
    if (!c || !c->subscriber || !c->subscriber->srtp) return;
    Peer* subscriber = c->subscriber;

    uint64_t ntp = getNtpTimestamp();
    uint32_t ntpHi = static_cast<uint32_t>(ntp >> 32);
    uint32_t ntpLo = static_cast<uint32_t>(ntp & 0xFFFFFFFF);

    uint8_t sr[28];
    sr[0] = 0x80; sr[1] = 200;
    uint16_t l = htons(6); memcpy(sr+2, &l, 2);
    uint32_t sss = htonl(c->rewrittenSsrc); memcpy(sr+4, &sss, 4);
    uint32_t hi = htonl(ntpHi); memcpy(sr+8, &hi, 4);
    uint32_t lo = htonl(ntpLo); memcpy(sr+12, &lo, 4);
    uint32_t rtpTs = htonl(c->lastSentTimestamp); memcpy(sr+16, &rtpTs, 4);
    uint32_t pc = htonl(c->packetsSent); memcpy(sr+20, &pc, 4);
    uint32_t oc = htonl(c->octetsSent); memcpy(sr+24, &oc, 4);

    uint8_t out[64]; memcpy(out, sr, 28); int outLen = 28;
    if (subscriber->srtp->protectRtcp(out, &outLen)) {
        _udp->sendTo(out, static_cast<size_t>(outLen), subscriber->remoteEp);
        c->lastSrNtpTs = ntpHi;  // 中间 32 位（简化）
        c->lastSrRtpTs = c->lastSentTimestamp;
    }
}
