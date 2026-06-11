# NameCon 技术方案

> 版本: v1.0 | 日期: 2026-06-11

---

## 一、项目概述

NameCon 是一个轻量级 WebRTC SFU 视频会议系统，使用 C++ 实现媒体引擎、Go 实现信令服务、浏览器原生 WebRTC API 作为前端。

### 1.1 核心目标

- 支持多人实时音视频通话（SFU 架构）
- 浏览器零依赖接入（原生 WebRTC API）
- 基于 DTLS/SRTP 的安全媒体传输
- 预留 AI 对话总结扩展位

> **MVP 限定**：localhost、Chrome 浏览器、host candidate、VP8/Opus、rtcp-mux、bundle、单路音视频。目标是"从零实现轻量级 WebRTC SFU 核心链路"，非生产级视频会议。

### 1.2 技术选型理由

| 决策 | 选型 | 理由 |
|------|------|------|
| 媒体层语言 | C++17 | 对 DTLS/SRTP 需要精细控制，C 库（OpenSSL/libsrtp）原生接口 |
| 信令层语言 | Go | 高并发 WebSocket 管理、REST API 开发效率高 |
| 前端 | 浏览器 WebRTC | 零 SDK 依赖，Chrome/Edge 内置 |
| 服务间通信 | gRPC (protobuf) | 强类型契约、跨语言、高性能二进制序列化 |
| 构建 | CMake + Go Module | C++/Go 各自生态标准工具 |
| 网络 I/O | boost::asio | C++ 异步 I/O 事实标准，header-only |
| 配置 | YAML | 可读性好，Go/C++ 均有成熟库 |

---

## 二、系统架构

### 2.1 架构全景图

```
┌──────────────────────────────────────────────────────────┐
│                    浏览器 (Chrome/Edge)                    │
│                                                          │
│  getUserMedia / getDisplayMedia / RTCPeerConnection      │
│                                                          │
│  ┌──────────┐  WebSocket (信令)     SRTP/UDP (媒体)      │
│  │ index.html│──────┬──────────────────────┐              │
│  │ room.html │      │                      │              │
│  │ api.js    │      ▼                      ▼              │
│  │ signaling │  ┌──────────┐         ┌──────────────┐    │
│  │ webrtc.js │  │signal-svc│  gRPC   │  media-svc   │    │
│  └──────────┘  │  Go      │◄───────►│  C++         │    │
│                │ :8080    │         │ :50051       │    │
│                │ :8081/ws │         │ UDP 10000-   │    │
│                └──────────┘         │ 20000        │    │
│                                     └──────────────┘    │
└──────────────────────────────────────────────────────────┘
```

### 2.2 服务职责

#### signal-svc (Go)

| 模块 | 职责 | 状态 |
|------|------|:--:|
| `api/` | REST API：房间 CRUD、Token 签发、健康检查 | 🔲 |
| `signaling/` | WebSocket Hub：连接管理、SDP/ICE 消息转发 | 🔲 |
| `room/` | 房间生命周期、参与者列表 | 🔲 |
| `sfu/` | gRPC Client：调用 C++ media-svc | ✅ |
| `auth/` | JWT 生成与验证 | 🔲 |
| `store/` | 存储接口（内存实现） | 🔲 |
| `summary/` | AI 总结服务 | 🔲 |

#### media-svc (C++)

| 模块 | 职责 | 状态 |
|------|------|:--:|
| `config/` | YAML 配置加载 + ConfigMgr 单例 | ✅ |
| `grpc/` | gRPC Server + MediaServiceImpl | ✅ |
| `transport/UdpServer` | UDP Socket 异步收发 | ✅ |
| `transport/IceServer` | ICE-lite：STUN Binding 响应 | 🔲 |
| `transport/DtlsContext` | DTLS 握手（OpenSSL） | 🔲 |
| `transport/SrtpContext` | SRTP 加解密（libsrtp） | 🔲 |
| `sdp/SdpParser` | SDP offer 解析 + answer 生成（fingerprint/ice-ufrag/codec） | 🔲 |
| `rtp/RtpHeader` | RTP 头解析（SSRC/PT/SeqNum/extension/padding） | 🔲 |
| `rtcp/RtcpHandler` | RTCP 最小实现（PLI/NACK/SR） | 🔲 |
| `sfu/Router` | 主循环：收包→解密→路由→加密→转发 | 🔲 |
| `sfu/Room` | 房间实体（C++ 侧） | 🔲 |
| `sfu/Peer` | 参会者（SSRC/密钥/地址） | 🔲 |
| `sfu/RouteTable` | SSRC→Peer 映射表（O(1)） | 🔲 |
| `sfu/Forwarder` | RTP 包转发器 | 🔲 |
| `ai/Pipeline` | AI 管线总控 | 🔲 |
| `utils/Singleton` | 单例模板 | ✅ |
| `utils/Logger` | 日志封装 | 🔲 |

---

## 三、核心数据流

### 3.1 创建房间流程

```
浏览器 A                    signal-svc (Go)              media-svc (C++)
───────                    ──────────────               ──────────────
POST /api/rooms ─────────▶ room_handler.go
                           │ 创建 Room 实体
                           │
                           ├─ gRPC CreateRoom ──────────────────▶ CreateRoom()
                           │                                     生成 room_id + token
                           ◀──────────────────────────────────── 返回
                           │
          {roomId, token} ◀─ HTTP 201
```

### 3.2 加入房间 + 建立媒体连接

```
浏览器 A               signal-svc (Go)                  media-svc (C++)
───────                ──────────────                  ──────────────
WS join ─────────────▶ hub.go
                       ├─ gRPC AddPeer(A) ────────────────────▶ AddPeer()
                       │                                        分配 UDP 端口
                       │                                        创建 DTLS 上下文
                       ◀─────────────────────────────────────── {sfu_ip, sfu_port}
                       │
      {peerId, sfu} ◀── WS joined

PC.createOffer() ────▶ WS offer ──────▶ gRPC SendOffer ───────▶ 解析 SDP
                                                                   生成 answer
      WS answer ◀────────────────────────── gRPC answer ◀──────── 返回

DTLS 握手 ───────────── UDP DTLS 包 ──────────────────────────▶ DtlsContext
                                                                   协商 SRTP 密钥
SRTP 加密媒体 ───────── UDP SRTP 包 ──────────────────────────▶ SrtpContext
                                                                   解密→路由→重加密→转发
```

### 3.3 SFU 转发核心

```
收 UDP 包 → 查 endpoint 找 Peer → SRTP 解密 → 读 RTP 头 12 字节
→ SSRC 查 Room → 遍历其他 Peer → 用目标密钥 SRTP 重加密 → UDP 发出
```

---

## 四、关键技术点

### 4.1 为什么 SFU 不解码

RTP 包的 12 字节头包含 SSRC（同步源标识符），SFU 只需读这 12 字节即可完成路由，不需要理解 H.264/Opus 载荷内容。

### 4.2 DTLS-SRTP 密钥协商

```
浏览器                    SFU (C++)
───────                   ─────────
DTLS ClientHello ──────▶ 生成自签证书
                         交换证书
DTLS 握手完成 ──────────▶ 双方导出 SRTP 密钥材料
                         每个 PeerConnection 独立的密钥
```

### 4.3 O(1) 路由查表

```cpp
ssrcToPeer_[ssrc]         → peerId       // SSRC 找 Peer
peerTable_[peerId]        → PeerInfo     // Peer 信息
roomPeers_[roomId]        → [peerId...]  // 房间成员列表
```

三张哈希表，全部 O(1) 查找。按房间分片，同房间内串行处理，无需锁。

### 4.4 AI 总结管线（预留）

```
音频 RTP → VAD(语音检测) → Opus解码 → whisper.cpp ASR → 文本积累
→ 每5分钟/手动/终场触发 → llama.cpp 推理 → 结构化 JSON
→ gRPC 回传 Go → WebSocket 推送给所有客户端
```

---

## 五、部署架构

```
docker-compose.yml
├── media-svc    (C++, :50051 gRPC, :10000-20000 UDP)
├── signal-svc   (Go, :8080 HTTP, :8081 WS)
└── web          (nginx, :3000)
```
