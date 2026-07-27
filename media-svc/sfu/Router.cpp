#include "Router.h"
#include "Peer.h"
#include "../transport/UdpServer.h"
#include "../transport/SrtpContext.h"
#include "../rtp/RtpHeader.h"
#include "../sdp/SdpParser.h"
#include <cstring>
#include <iostream>
#include <algorithm>

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
        subscriber->consumers.push_back(std::move(c));
    }

    std::cout << "[Router] AddConsumer: " << subscriber->peerId
              << " <- " << publisher->peerId
              << " (" << (isVideo ? "video" : "audio")
              << ") outSsrc=" << raw->rewrittenSsrc << std::endl;
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
    std::cout << "[Router] Peer removed: " << peer->peerId << std::endl;
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

    bool isVideo = (hdr.payloadType == SdpParser::videoPT);
    Producer* producer;
    std::vector<Consumer*> targets;
    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        producer = findOrCreateProducer(fromPeer, hdr.ssrc, isVideo);
        if (!producer) return;
        targets = producer->consumers;  // 快照
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
    std::shared_lock<std::shared_mutex> lock(_mutex);
    for (auto& c : subscriber->consumers) {
        if (c->isVideo == isVideo && c->publisher && c->publisher->peerId == publisherPeerId) {
            c->recvMid = recvMid;
            std::cout << "[Router] bindConsumerByMid: " << subscriber->peerId
                      << " mid=" << recvMid << " -> outSsrc=" << c->rewrittenSsrc << std::endl;
            return c->rewrittenSsrc;
        }
    }
    std::cerr << "[Router] bindConsumerByMid: no consumer for "
              << subscriber->peerId << " <- " << publisherPeerId
              << " (" << (isVideo ? "video" : "audio") << ")" << std::endl;
    return 0;
}

uint32_t Router::findRewrittenSsrc(Peer* subscriber, const std::string& recvMid) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    for (auto& c : subscriber->consumers) {
        if (c->recvMid == recvMid) return c->rewrittenSsrc;
    }
    return 0;
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

// === RTCP 翻译：Task 6 实现具体逻辑，先空实现占位 ===
void Router::onRtcpPacket(const uint8_t*, size_t, Peer*) {}
void Router::handlePLI(const uint8_t*, size_t, Peer*) {}
void Router::handleNACK(const uint8_t*, size_t, Peer*) {}
void Router::handleRR(const uint8_t*, size_t, Peer*) {}
void Router::generateAndSendSR(Consumer*) {}
