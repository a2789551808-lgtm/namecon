#pragma once
#include <unordered_map>
#include <cstdint>

struct Peer;

// O(1) SSRC → Peer 查表
// 每个 SSRC 只属于一个 Peer（一个 Peer 可有多个 SSRC：音频+视频）
class RouteTable {
public:
    // 按 SSRC 查找 → 找到则返回 Peer*, 否则 nullptr
    Peer* lookup(uint32_t ssrc) const;

    // 绑定 SSRC → Peer（首次见到该 SSRC 时调用）
    void bind(uint32_t ssrc, Peer* peer);

    // 解绑某 Peer 的全部 SSRC（Peer 离开时调用）
    void unbindPeer(Peer* peer);

private:
    std::unordered_map<uint32_t, Peer*> _ssrcToPeer;
};
