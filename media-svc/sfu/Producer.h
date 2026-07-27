// media-svc/sfu/Producer.h
#pragma once
#include <cstdint>
#include <vector>

struct Peer;
struct Consumer;

// 一个 Producer = 一路发送方的流
// 由 Router 在首次见到该 SSRC 时自动创建，关联所有订阅该流的 Consumer
struct Producer {
    Peer* publisher = nullptr;
    uint32_t originalSsrc = 0;   // 发送方原始 SSRC
    bool isVideo = false;
    std::vector<Consumer*> consumers;  // 订阅该流的所有 Consumer（裸指针，归属各 Peer::consumers）
};
