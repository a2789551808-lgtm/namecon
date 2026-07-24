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

// addForwarding：建立单向转发关系
// 做什么：把 toPeer 加入 fromPeer->forwardTo 列表，使 fromPeer 收到的 RTP 包
//         会被转发给 toPeer。调用方为 Go 侧信令服务（通过 gRPC 调用），
//         在房间内"订阅"某个发布者时触发。
// 参数含义：
//   fromPeer - 发布者 Peer，后续其 RTP 流将被转发
//   toPeer   - 订阅者 Peer，作为转发目标接收数据
void Router::addForwarding(Peer* fromPeer, Peer* toPeer) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    auto& targets = fromPeer->forwardTo;
    if (std::find(targets.begin(), targets.end(), toPeer) == targets.end()) {
        targets.push_back(toPeer);
        std::cout << "[Router] Forwarding: " << fromPeer->peerId
                  << " -> " << toPeer->peerId << std::endl;
    }
}

// removePeer：移除一个 Peer 的所有路由信息
// 做什么：从路由表解绑该 Peer 的所有 SSRC 映射，并清空其 forwardTo 列表，
//         使得该 Peer 既不再接收转发也不再被转发。
//         调用方为 Go 侧信令服务（通过 gRPC 调用），在用户离开房间时触发。
// 参数含义：
//   peer - 待移除的 Peer 指针
void Router::removePeer(Peer* peer) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _table->unbindPeer(peer);
    peer->forwardTo.clear();
    std::cout << "[Router] Peer removed: " << peer->peerId << std::endl;
}

// ============================================================
// RTP 转发核心（io_context 线程，读锁）
// ============================================================

// onRtpPacket：RTP 包转发核心入口
// 做什么：把收到的明文 RTP 包按转发关系重新加密后发送给所有目标 Peer。
//         完整流程为：解析 RTP 头拿 SSRC -> 查路由表确定发送者 -> 拷贝转发目标
//         快照 -> 对每个目标用其 SRTP 上下文重新加密 -> 通过 UDP 发出。
// 参数含义：
//   plainRtp - 已解密的明文 RTP 数据指针
//   len      - 数据长度
//   fromPeer - 数据来源 Peer（由 UDP 收包逻辑传入，用于首次 SSRC 绑定）
// 为什么用 SdpParser::videoPT 判断视频 SSRC：
//   SDP 协商时 parseOffer 会把视频的 payload type 存入全局静态变量
//   SdpParser::videoPT。当首次见到某个 SSRC 时，无法从 SSRC 本身区分音视频，
//   只能靠 RTP 头里的 payload type 与 videoPT 比对来决定该 SSRC 是音频还是视频。
// 为什么拷贝 forwardTo 快照：
//   addForwarding / removePeer 会在其他线程写持写锁修改 forwardTo 列表。
//   转发流程持读锁期间先做一次拷贝得到快照，之后遍历和发送都在快照上进行，
//   这样既缩短了读锁持有时间，又避免遍历过程中列表被修改导致迭代器失效。
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
