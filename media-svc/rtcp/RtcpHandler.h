#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>

// RTCP 处理器 — 解析浏览器发来的 RTCP 复合包 + 构造 SFU 发出的 RTCP 包
//
// RTCP 复合包结构 (RFC 3550):
//   每个复合包至少包含: SR/RR + SDES(CNAME)
//   可选附加: PLI (PSFB, FMT=1) / NACK (RTPFB, FMT=1) / FIR 等
//
// 协议检测: SRTP 解密后, byte[1] (PT) 在 200~223 范围 → RTCP, 否则 → RTP

class RtcpHandler {
public:
    // 发送回调 — SFU 需要通过 UDP 发出 RTCP 包
    using SendCallback = std::function<void(const uint8_t* data, size_t len)>;

    // 解析到 PLI 时回调 — 参数: mediaSsrc (哪个 SSRC 需要关键帧)
    using PliCallback = std::function<void(uint32_t mediaSsrc)>;

    // 解析到 NACK 时回调 — 参数: mediaSsrc, pid (丢失的包序号), blp (后续16包位图)
    using NackCallback = std::function<void(uint32_t mediaSsrc, uint16_t pid, uint16_t blp)>;

    // 解析到 SDES 时回调 — 参数: ssrc, cname
    using SdesCallback = std::function<void(uint32_t ssrc, const std::string& cname)>;

    // === 回调设置 ===
    void setSendCallback(SendCallback cb)   { _sendCb = std::move(cb); }
    void setPliCallback(PliCallback cb)     { _pliCb = std::move(cb); }
    void setNackCallback(NackCallback cb)   { _nackCb = std::move(cb); }
    void setSdesCallback(SdesCallback cb)   { _sdesCb = std::move(cb); }

    // === 解析收到浏览器的 RTCP 复合包 ===
    void onRtcpPacket(const uint8_t* data, size_t len);

    // === 构造 SFU 发出的 RTCP 包 ===
    // 发送 PLI 给指定 sender（请求关键帧）
    void sendPLI(uint32_t senderSsrc, uint32_t mediaSsrc);

    // 发送 NACK 给指定 sender（请求重传丢包）
    void sendNACK(uint32_t senderSsrc, uint32_t mediaSsrc,
                  uint16_t pid, uint16_t blp);

private:
    SendCallback  _sendCb;
    PliCallback   _pliCb;
    NackCallback  _nackCb;
    SdesCallback  _sdesCb;
};
