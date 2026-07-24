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

Peer* Router::findPeerBySsrc(uint32_t ssrc) {
    return _table->lookup(ssrc);
}

// ============================================================
// 转发关系管理（Go 通过 gRPC 调用，写锁）
// ============================================================

void Router::addForwarding(Peer* fromPeer, Peer* toPeer) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    auto& targets = fromPeer->forwardTo;
    if (std::find(targets.begin(), targets.end(), toPeer) == targets.end()) {
        targets.push_back(toPeer);
        std::cout << "[Router] Forwarding: " << fromPeer->peerId
                  << " -> " << toPeer->peerId << std::endl;
    }
}

void Router::removePeer(Peer* peer) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _table->unbindPeer(peer);
    peer->forwardTo.clear();
    std::cout << "[Router] Peer removed: " << peer->peerId << std::endl;
}

// ============================================================
// RTP 转发核心（io_context 线程，读锁）
// ============================================================
void Router::onRtpPacket(const uint8_t* plainRtp, size_t len, Peer* fromPeer) {
    // ① 解析 RTP 头 -> 拿 SSRC
    RtpHeader hdr;
    if (!RtpHeader::parse(plainRtp, len, hdr)) return;

    // ② 查 SSRC -> 确定发送者 Peer
    Peer* sender = _table->lookup(hdr.ssrc);
    if (!sender) {
        _table->bind(hdr.ssrc, fromPeer);
        sender = fromPeer;
        // 首次见到这个 SSRC，用 SDP 解析出的视频 PT 判断是音频还是视频
        if (hdr.payloadType == SdpParser::videoPT) {
            sender->videoSsrc = hdr.ssrc;
        } else {
            sender->audioSsrc = hdr.ssrc;
        }
    }

    // ③ 加读锁，拷贝转发目标快照（防止遍历时被修改）
    std::vector<Peer*> targetsCopy;
    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        targetsCopy = sender->forwardTo;
    }

    if (targetsCopy.empty()) {
        static int emptyCount = 0;
        if (++emptyCount <= 5)
            std::cout << "[Router] No forward targets for SSRC=" << hdr.ssrc << std::endl;
        return;
    }

    // ④ 更新转发统计
    sender->forwardedPackets++;
    sender->forwardedOctets += static_cast<uint32_t>(len);

    // ⑤ 对每个目标：重加密 -> 发出
    for (auto* target : targetsCopy) {
        if (!target->srtp) {
            static int noSrtpCount = 0;
            if (++noSrtpCount <= 5)
                std::cout << "[Router] No SRTP for target " << target->peerId << std::endl;
            continue;
        }

        uint8_t out[65536];
        memcpy(out, plainRtp, len);
        int outLen = static_cast<int>(len);

        if (target->srtp->protect(out, &outLen)) {
            static int fwdCount = 0;
            if (++fwdCount <= 3) {
                std::cout << "[Router] FWD #" << fwdCount
                          << " SSRC=" << hdr.ssrc << " PT=" << (int)hdr.payloadType
                          << " seq=" << hdr.sequenceNumber
                          << " " << len << "->" << outLen << "B -> " << target->peerId << std::endl;
            }
            _udp->sendTo(out, static_cast<size_t>(outLen), target->remoteEp);
        } else {
            static int errCount = 0;
            if (++errCount <= 3) {
                std::cerr << "[Router] protect FAILED for " << target->peerId
                          << " (srtp inited=" << (target->srtp != nullptr) << ")" << std::endl;
            }
        }
    }
}
