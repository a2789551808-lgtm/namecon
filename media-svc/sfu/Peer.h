#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

class DtlsContext;
class SrtpContext;

// 参会者实体 — 一个 Peer = 一个浏览器的完整连接状态
struct Peer {
    std::string peerId;
    boost::asio::ip::udp::endpoint remoteEp;

    std::shared_ptr<DtlsContext> dtls;
    std::shared_ptr<SrtpContext> srtp;

    uint32_t audioSsrc = 0;
    uint32_t videoSsrc = 0;

    // 转发目标列表 — Go 信令通过 gRPC 设置"这个 Peer 的包应该转发给谁"
    std::vector<Peer*> forwardTo;

    // 转发统计
    uint32_t forwardedPackets = 0;
    uint32_t forwardedOctets  = 0;
};
