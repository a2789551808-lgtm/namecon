# NameCon — 项目概览

> C++ 媒体引擎 + Go 信令服务器 + Web 前端 | 校招项目

## 项目定位

从零实现一个 WebRTC SFU 视频会议系统。Go 负责信令调度与房间管理，C++ 负责媒体流的加密解密与选择性转发（SFU）。前端使用浏览器原生 WebRTC API，无需额外 SDK。预留 AI 对话总结扩展位。

---

## 架构全景

```
                        浏览器 (Chrome/Edge)
              ┌──────────────────────────────────┐
              │          WebRTC API (内置)         │
              │  getUserMedia / getDisplayMedia   │
              │  RTCPeerConnection                │
              └──────┬───────────────┬────────────┘
                     │ WebSocket     │  SRTP (UDP)
                     │ (信令交换)     │  (加密音视频)
                     ▼                ▼
┌──────────────────────────────┐  ┌────────────────────────────────┐
│   Go 信令服务 (signal-svc)    │  │   C++ 媒体服务 (media-svc)       │
│   :8080 HTTP / :8081 WS      │  │   :50051 gRPC / :10000-20000    │
│                              │  │   UDP                          │
│  ┌────────────────────────┐  │  │                                │
│  │ REST API               │  │  │  ┌──────────────────────────┐  │
│  │ 房间/用户/Token/总结     │  │  │  │ DTLS 握手 (OpenSSL)       │  │
│  └────────────────────────┘  │  │  └──────────────────────────┘  │
│  ┌────────────────────────┐  │  │  ┌──────────────────────────┐  │
│  │ WebSocket Hub          │  │  │  │ SRTP 加解密 (libsrtp)     │  │
│  │ SDP/ICE 消息转发        │  │  │  │ 解密 → 读 RTP 头 → 加密  │  │
│  └────────────────────────┘  │  │  └──────────────────────────┘  │
│  ┌────────────────────────┐  │  │  ┌──────────────────────────┐  │
│  │ 房间状态 (内存)          │  │  │  │ SFU 路由核心              │  │
│  └────────────────────────┘  │  │  │ SSRC→Room→Peers 映射表   │  │
│  ┌────────────────────────┐  │  │  │ O(1) 查表 → 转发         │  │
│  │ gRPC Client ─────────────┼──│──│▶└──────────────────────────┘  │
│  │ 调用 media-svc          │  │  │  ┌──────────────────────────┐  │
│  └────────────────────────┘  │  │  │ AI 总结管线 (预留)         │  │
│                              │  │  │ VAD → ASR → LLM 总结      │  │
│                              │  │  └──────────────────────────┘  │
│                              │  │  ┌──────────────────────────┐  │
│                              │  │  │ gRPC Server :50051        │  │
│                              │  │  │ 接收 signal-svc 指令       │  │
│                              │  │  └──────────────────────────┘  │
└──────────────────────────────┘  └────────────────────────────────┘
```

---

## 核心概念：为什么 SFU 不解码？

```
RTP 包结构：
┌────────────┬──────────────────────────┐
│ RTP Header │   Video Payload (H.264)   │
│  12 bytes  │   可变长度                 │
│            │                          │
│ 包含:       │   SFU 原样复制转发即可     │
│  SSRC      │   不需要解码、不需要理解    │
│  SeqNum    │   画面内容                │
│  Timestamp │                          │
│  PT        │                          │
└────────────┴──────────────────────────┘

C++ SFU 只读前 12 字节 → 查表 → 换密钥加密 → 发出
CPU 几乎无消耗。编码是客户端的事，解码也是客户端的事。
```

---

## 完整音视频通话流程 (一次全链路)

```
房主 A 创建房间，参会者 B 加入：

① A ──POST /api/rooms────────────▶ Go Server
   Go ──gRPC CreateRoom───────────▶ C++ SFU
   A ◀── {roomId, token}

② A ──WS join────────────────────▶ Go Server
   Go ──gRPC AddPeer(A)───────────▶ C++ SFU (分配 UDP 端口 10001)
   A ◀── {peerId, sfuIp, sfuPort}

③ A 创建 PeerConnection，createOffer()
   A ──WS offer SDP───────────────▶ Go ──gRPC SendOffer──▶ C++
   C++ 解析 SDP，生成 answer SDP
   A ◀──WS answer SDP───────────── Go ◀──gRPC answer──── C++

④ A 和 C++ SFU 交换 ICE Candidate (通过 Go 转发)
   A 和 C++ SFU 完成 DTLS 握手 → SRTP 密钥协商完成
   A ══SRTP(RTP, audio SSRC=100, video SSRC=101)══▶ C++

⑤ B 重复 ②③④，B 也连上 C++ SFU
   B ══SRTP(RTP, audio SSRC=200, video SSRC=201)══▶ C++

⑥ C++ SFU 转发 (核心!)
   A 的音频 SSRC=100 → 查表: room "abc", peers=[A,B]
   → 发给 B (用 B 的 SRTP 密钥重新加密)
   B ◀══SRTP(RTP, SSRC=100)══┘

   B 的视频 SSRC=201 → 查表: room "abc", peers=[A,B]
   → 发给 A (用 A 的 SRTP 密钥重新加密)
   A ◀══SRTP(RTP, SSRC=201)══┘

   ✅ A 和 B 互相看到听到对方
```

---

## WebSocket 信令协议

```
客户端 → 服务器:
┌─────────────────┬──────────────────────────────────┐
│ 类型             │ 说明                              │
├─────────────────┼──────────────────────────────────┤
│ join            │ 加入房间 (roomId + token)          │
│ leave           │ 离开房间                           │
│ offer           │ SDP offer → 转发给 C++ SFU        │
│ ice-candidate   │ ICE Candidate → 转发给 C++ SFU    │
│ mute            │ 静音/关闭摄像头                     │
│ request-summary │ 手动触发 AI 总结 (预留)             │
└─────────────────┴──────────────────────────────────┘

服务器 → 客户端:
┌─────────────────┬──────────────────────────────────┐
│ 类型             │ 说明                              │
├─────────────────┼──────────────────────────────────┤
│ joined          │ 成功加入 (peerId, sfuInfo)         │
│ peer-joined     │ 新成员加入                         │
│ peer-left       │ 成员离开                           │
│ answer          │ 来自 SFU 的 SDP answer             │
│ ice-candidate   │ 来自 SFU 的 ICE Candidate          │
│ peer-muted      │ 某人静音/关闭摄像头                  │
│ summary         │ AI 总结结果 (预留)                  │
│ error           │ 错误信息                           │
└─────────────────┴──────────────────────────────────┘
```

---

## gRPC 协议 (Go ↔ C++)

```protobuf
syntax = "proto3";
package media;

service MediaService {
  rpc CreateRoom(CreateRoomReq) returns (CreateRoomResp);
  rpc DestroyRoom(DestroyRoomReq) returns (DestroyRoomResp);
  rpc AddPeer(AddPeerReq) returns (AddPeerResp);
  rpc RemovePeer(RemovePeerReq) returns (RemovePeerResp);
  rpc SendOffer(SendOfferReq) returns (SendOfferResp);
  rpc SendIceCandidate(SendIceCandidateReq) returns (SendIceCandidateResp);
  rpc MuteTrack(MuteTrackReq) returns (MuteTrackResp);
  rpc GenerateSummary(SummaryReq) returns (SummaryResp);   // 预留
  rpc GetRoomStats(RoomStatsReq) returns (RoomStatsResp);
}
```

---

## REST API

```
POST   /api/rooms                    创建房间
GET    /api/rooms/:id                查询房间信息
POST   /api/rooms/:id/join           获取入会 Token
DELETE /api/rooms/:id                结束房间 (房主)
GET    /api/rooms/:id/participants   参与者列表
POST   /api/rooms/:id/kick           踢人 (房主)
POST   /api/rooms/:id/summary        手动触发 AI 总结 (预留)
GET    /api/health                   健康检查
```

---

## C++ SFU 路由核心伪代码

```cpp
// 核心数据结构：三张哈希表，全部 O(1) 查找

// SSRC → PeerId
std::unordered_map<uint32_t, std::string> ssrcToPeer_;

// PeerId → PeerInfo
struct PeerInfo {
    std::string peerId;
    std::string roomId;
    uint32_t    audioSSRC;
    uint32_t    videoSSRC;
    srtp_t      srtpOut;       // 发给此 Peer 的加密上下文
    udp::endpoint remoteEp;    // 浏览器 UDP 地址
};
std::unordered_map<std::string, PeerInfo> peerTable_;

// RoomId → [PeerId...]
std::unordered_map<std::string, std::vector<std::string>> roomPeers_;


// 主循环
void Router::run() {
    while (running_) {
        // ① 收包
        auto [data, len, fromEp] = udpServer_.recv();

        // ② 找到 Peer
        auto peerId = findPeerByEndpoint(fromEp);
        auto& peer = peerTable_[peerId];

        // ③ SRTP 解密
        uint8_t rtpBuf[65536];
        int rtpLen = srtp_unprotect(peer.srtpIn, data, len, rtpBuf);

        // ④ 读 RTP 头 (只读 12 字节)
        uint32_t ssrc = *(uint32_t*)(rtpBuf + 8);
        uint16_t seq  = *(uint16_t*)(rtpBuf + 2);

        // ⑤ 查房间
        auto roomId = peer.roomId;

        // ⑥ 转发给房间其他人
        for (auto& targetId : roomPeers_[roomId]) {
            if (targetId == peerId) continue;
            auto& target = peerTable_[targetId];

            // SRTP 重新加密 (用目标的密钥)
            uint8_t outBuf[65536];
            int outLen = srtp_protect(target.srtpOut, rtpBuf, rtpLen, outBuf);

            // 发出
            udpServer_.send(outBuf, outLen, target.remoteEp);
        }

        // ⑦ 同时送入 AI 管线 (预留)
        aiPipeline_.onRtpPacket(peerId, rtpBuf, rtpLen);
    }
}
```

---

## AI 总结功能 (预留设计)

### 当前状态 (Phase 1)

```cpp
// ai/pipeline.h — 空壳实现，不影响音视频通话

class AIPipeline {
public:
    void init(bool enabled) { enabled_ = enabled; }

    // 每个音频 RTP 包经过时调用
    void onAudioPacket(PeerId peer, const uint8_t* rtpPayload, size_t len) {
        if (!enabled_) return;
        // TODO Phase 3: VAD → 解码 Opus → whisper.cpp → 文本积累
    }

    // Go Server 通过 gRPC 触发总结
    SummaryResult generateSummary(TriggerMode mode) {
        if (!enabled_) return {};
        // TODO Phase 3: 积累的文本 → llama.cpp → 结构化 JSON
        return {};
    }

private:
    bool enabled_ = false;
    // TODO: whisper_context* asrCtxs_;
    // TODO: TranscriptBuffer transcripts_;
    // TODO: LLMEngine llm_;
};
```

### 未来实现 (Phase 3)

```
AIPipeline 完整流程：

音频 RTP 包到达
    │
    ▼
VAD (WebRTC VAD, C 实现) → 判断是否有语音
    │
    ├── 无人声 → 跳过
    │
    └── 有人声 → 按 PeerId 分组
         │
         ▼
    Opus 解码 → PCM (libopus, 轻量)
         │
         ▼
    whisper.cpp 流式转录 → 文字
         │
         ▼
    TranscriptBuffer (内存存储)
         │
         ▼ (每5分钟 / 手动触发 / 终场)
    llama.cpp → Qwen2.5-1.5B INT4 推理
         │
         ▼
    结构化 JSON: {
      title, overview, keyPoints[], actionItems[]
    }
         │
         ▼
    gRPC 回调 → Go Server → WebSocket 推送给所有客户端
```

### 模型选型 (预留)

| 模块 | 模型 | 大小 | 推理速度 (CPU) |
|------|------|------|---------------|
| VAD | WebRTC VAD | ~10KB | < 1ms |
| ASR | whisper.cpp tiny | ~75MB | ~20ms/秒音频 |
| LLM | Qwen2.5-1.5B INT4 | ~1GB | ~800ms/次 |

---

## 开发阶段

```
Phase 1 ── MVP 音视频通话 ── 2~3 周
├── Go: REST API + WebSocket + 房间管理
├── C++: UDP + DTLS + SRTP + 基础转发
├── Go↔C++: gRPC 打通
├── 前端: 两人互通
└── 验收: 两个浏览器标签页互相看到听到

Phase 2 ── 多人会议 ── 1 周
├── 多人视频网格布局
├── 静音/关闭摄像头控制
├── 屏幕共享
└── 验收: 4 人会议 + 屏幕共享

Phase 3 ── AI 总结 ── 2 周
├── VAD + whisper.cpp ASR
├── llama.cpp 总结生成
├── 定时/手动/终场三种模式
├── 前端展示总结面板
└── 验收: 讨论 5 分钟后生成结构化总结

Phase 4 ── 文档 ── 1 周
├── 架构图 + 序列图
├── README + Quick Start
├── API 文档
└── 面试自述话术
```

---

## 配置文件示例

### signal-svc.yaml

```yaml
server:
  http_port: 8080
  ws_port: 8081

jwt:
  secret: "change-me-in-production"
  expire_hours: 24

media_service:
  host: "127.0.0.1"
  port: 50051
```

### media-svc.yaml

```yaml
server:
  grpc_port: 50051
  udp_port_start: 10000       # Peer UDP 端口范围起始
  udp_port_end: 20000         # Peer UDP 端口范围结束
  public_ip: "127.0.0.1"      # 生产环境改成公网 IP

dtls:
  cert_file: "./certs/cert.pem"
  key_file: "./certs/key.pem"

ai:
  enabled: false              # AI 总结开关
  vad_sensitivity: 2          # 0=低 1=中 2=高 3=极高
  summary_interval_min: 5     # 定时总结间隔(分钟)
```

---

## 面试准备要点

### 核心技术亮点

1. **完整 WebRTC 传输层**：DTLS 握手 + SRTP 加解密，非调库拼接
2. **SFU 路由零拷贝**：只读 RTP 包头 12 字节，不解码视频 payload
3. **O(1) 路由查表**：三张哈希表实现 SSRC → Room → Peers 映射
4. **密钥隔离**：每个 PeerConnection 独立 SRTP 上下文
5. **Go+C++ 微服务**：gRPC 通信，职责清晰分离
6. **预留 AI 总结**：VAD → ASR → LLM 全链路设计

### 必问清单

```
Q: 为什么不用 FFmpeg？
A: SFU 不做编解码。编码在浏览器端完成（硬件加速），
   SFU 只读 RTP 头做路由，不解码视频载荷。省下来的 CPU
   留给未来的 AI 推理。

Q: 怎么处理多线程？
A: 按房间分片，每个房间一个事件循环线程。
   跨房间无竞争，同房间内串行处理，无需锁。

Q: DTLS 证书怎么管理？
A: SFU 启动时自签一个证书，SDP answer 里带指纹。
   浏览器验证指纹即完成 DTLS 握手。

Q: 丢包怎么办？
A: Phase 1 不做 NACK 重传（保持核心清晰）。
   Phase 3 可以加入 NACK + PLI/FIR 处理。

Q: 为什么不用 LiveKit/Pion？
A: 深入理解协议层。LiveKit 封装好了，看不到 DTLS/SRTP
   细节。自己实现才能掌握音视频的底层原理。
```

---

## 技术栈总览

| 层 | 技术 | 备注 |
|----|------|------|
| 前端 | 浏览器原生 WebRTC API | 零外部依赖 |
| Go HTTP | `net/http` + `gorilla/mux` | |
| Go WS | `gorilla/websocket` | |
| Go gRPC | `google.golang.org/grpc` | |
| Go 日志 | `zap` | |
| C++ 网络 | `boost::asio` 1.83+ | 异步 UDP |
| C++ DTLS | OpenSSL 3.0+ | |
| C++ SRTP | libsrtp 2.5+ | |
| C++ gRPC | gRPC C++ 1.51+ | |
| C++ 日志 | `spdlog` 1.12+ | header-only |
| C++ AI (未来) | whisper.cpp + llama.cpp + libopus | 纯 C/C++ CPU 推理 |
| 构建 | CMake 3.20+ / Go Module | |
| 部署 | Docker + Docker Compose | |
