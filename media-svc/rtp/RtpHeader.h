#pragma once
#include <cstddef>
#include <cstdint>

// RTP 头结构 — 不能假定固定 12 字节
// 需要正确解析 CSRC、header extension、padding
struct RtpHeader {
    uint8_t  version;
    bool     padding;
    bool     extension;
    uint8_t  csrcCount;
    bool     marker;
    uint8_t  payloadType;
    uint16_t sequenceNumber;
    uint32_t timestamp;
    uint32_t ssrc;

    // 从原始数据解析 — 使用 memcpy+ntohl，避免未对齐和大小端问题
    static bool parse(const uint8_t* data, size_t len, RtpHeader& out);
    // 获取 RTP 头总长度（含 CSRC 和 extension）
    static size_t headerLength(const uint8_t* data, size_t len);
};
