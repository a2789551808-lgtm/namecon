#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// AI 管线总控 (预留)
class AIPipeline {
public:
    void init(bool enabled);
    void onAudioPacket(const std::string& peerId, const uint8_t* data, size_t len);
};
