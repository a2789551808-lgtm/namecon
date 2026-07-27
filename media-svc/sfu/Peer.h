#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include "Consumer.h"
#include "Producer.h"

class DtlsContext;
class SrtpContext;

// 参会者实体 — 一个 Peer = 一个浏览器的完整连接状态
struct Peer {
    std::string peerId;
    boost::asio::ip::udp::endpoint remoteEp;

    std::shared_ptr<DtlsContext> dtls;
    std::shared_ptr<SrtpContext> srtp;

    // === Consumer 模型 ===
    std::vector<std::unique_ptr<Producer>> producers;  // 该 Peer 发送的流（通常 audio + video）
    std::vector<std::unique_ptr<Consumer>> consumers;   // 该 Peer 订阅的流
};
