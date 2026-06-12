#pragma once
#include "RouteTable.h"
#include <memory>

struct Peer;
class UdpServer;

// SFU 路由核心 — 只看两样：SSRC→Peer（RouteTable），Peer→转发目标（forwardTo）
// 房间管理、Peer 增删等业务逻辑由 Go 信令侧负责
class Router {
public:
    Router(std::shared_ptr<UdpServer> udp,
           std::shared_ptr<RouteTable> table);

    // === 转发关系管理（Go 通过 gRPC 调用） ===
    // 设置 fromPeer 的媒体流应转发给 toPeer
    void addForwarding(Peer* fromPeer, Peer* toPeer);

    // 移除 Peer 的所有转发关系（Peer 离开时调用）
    void removePeer(Peer* peer);

    // === RTP 转发 ===
    // PacketRouter 解密后调用 — 明文 RTP 包，来自 fromPeer
    void onRtpPacket(const uint8_t* plainRtp, size_t len, Peer* fromPeer);

private:
    std::shared_ptr<UdpServer>  _udp;
    std::shared_ptr<RouteTable> _table;
};
