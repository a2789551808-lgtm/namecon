#include "Router.h"
#include "Peer.h"
#include "../transport/UdpServer.h"
#include "../transport/SrtpContext.h"
#include "../rtp/RtpHeader.h"
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
// 转发关系管理（Go 通过 gRPC 调用）
// ============================================================

void Router::addForwarding(Peer* fromPeer, Peer* toPeer) {
    // 避免重复添加
    auto& targets = fromPeer->forwardTo;
    if (std::find(targets.begin(), targets.end(), toPeer) == targets.end()) {
        targets.push_back(toPeer);
        std::cout << "[Router] Forwarding: " << fromPeer->peerId
                  << " → " << toPeer->peerId << std::endl;
    }
}

void Router::removePeer(Peer* peer) {
    // ① 从其他 Peer 的 forwardTo 列表里删除 peer
    _table->unbindPeer(peer);

    // ② 清理 peer 自己的转发列表
    peer->forwardTo.clear();

    std::cout << "[Router] Peer removed: " << peer->peerId << std::endl;
}

// ============================================================
// RTP 转发核心
// ============================================================
void Router::onRtpPacket(const uint8_t* plainRtp, size_t len, Peer* fromPeer) {
    // ① 解析 RTP 头 → 拿 SSRC
    RtpHeader hdr;
    if (!RtpHeader::parse(plainRtp, len, hdr)) return;

    // ② 查 SSRC → 确定发送者 Peer
    Peer* sender = _table->lookup(hdr.ssrc);
    if (!sender) {
        _table->bind(hdr.ssrc, fromPeer);
        sender = fromPeer;
    }

    // ③ sender 的转发目标
    auto& targets = sender->forwardTo;
    if (targets.empty()) return;

    // ④ 更新转发统计
    sender->forwardedPackets++;
    sender->forwardedOctets += static_cast<uint32_t>(len);

    // ⑤ 对每个目标：重加密 → 发出
    for (auto* target : targets) {
        if (!target->srtp) continue;

        uint8_t out[65536];
        memcpy(out, plainRtp, len);
        int outLen = static_cast<int>(len);

        if (target->srtp->protect(out, &outLen)) {
            _udp->sendTo(out, static_cast<size_t>(outLen), target->remoteEp);
        }
    }
}
