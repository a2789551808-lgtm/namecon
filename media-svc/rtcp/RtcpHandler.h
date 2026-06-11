#pragma once
#include <cstdint>
#include <functional>

// RTCP 最小实现 — PLI (关键帧请求) + NACK (丢包重传) + SR (发送报告)
class RtcpHandler {
public:
    using SendCallback = std::function<void(const uint8_t* data, size_t len)>;

    void setSendCallback(SendCallback cb);

    // 处理收到的 RTCP 包
    void onRtcpPacket(const uint8_t* data, size_t len);

    // 构造并发送 PLI (Picture Loss Indication)
    void sendPLI(uint32_t ssrc);

    // 构造并发送 NACK (Negative ACK) — 可延后
    void sendNACK(uint32_t ssrc, uint16_t seqNum);

private:
    SendCallback _sendCb;
};
