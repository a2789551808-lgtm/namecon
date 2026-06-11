# NameCon 开发计划

> 版本: v2.0 | 日期: 2026-06-11 | 当前: Phase 2 Day 6-7 完成

---

## MVP 定位

> **限定范围**：localhost、Chrome 浏览器、host candidate、VP8/Opus、rtcp-mux、bundle、单路音视频。目标是"从零实现轻量级 WebRTC SFU 核心链路"，非生产级视频会议。

---

## 总览

| 阶段 | 目标 | 周期 | 状态 |
|------|------|:--:|:--:|
| Phase 1 | 项目骨架 + gRPC 通信 | 1 周 | ✅ 完成 |
| Phase 2 | WebRTC 最小闭环 | 4~5 周 | 🔜 |
| Phase 3 | RTCP 补全 + 多人 SFU | 1~2 周 | ⬜ |
| Phase 4 | AI 总结 + 文档 | 2 周 | ⬜ |

---

## Phase 1 — 项目骨架 ✅

> 已完成于 2026-06-09

| 任务 | 产出 | 状态 |
|------|------|:--:|
| 环境安装 | 19 项依赖全部通过 | ✅ |
| Proto 定义 | `media.proto` + gen_proto.sh 生成代码 | ✅ |
| Go 骨架编译 | signal-svc 编译通过 | ✅ |
| C++ 骨架编译 | media-svc 编译通过 | ✅ |
| gRPC 打通 | Go Client ↔ C++ Server CreateRoom 往返成功 | ✅ |
| UdpServer | boost::asio 异步 UDP，5 线程多核 | ✅ |
| 构建系统 | `make build` / `make run` / `make clean` | ✅ |
| 开发规范 | `docs/rules.md` (C++ + Go) | ✅ |

**验收**: 两个二进制启动，gRPC 往返成功，UDP 收发正常 ✅

---

## Phase 2 — WebRTC 最小闭环 🔜

> 预计 2026-06-11 ~ 2026-07-10（约 4 周）  
> 目标：两个 Chrome 标签页 localhost 互通音视频

### Day 8-10: ICE-lite + SDP

```
新增文件:
  media-svc/transport/IceServer.h/.cpp     — ICE-lite
  media-svc/sdp/SdpParser.h/.cpp           — SDP 解析+生成

任务:
  [ ] ICE-lite: 解析浏览器 STUN Binding Request
  [ ] ICE-lite: 回复 STUN Binding Response (XOR-MAPPED-ADDRESS)
  [ ] SDP: 解析浏览器 Offer (ice-ufrag/ice-pwd/fingerprint/codec/ssrc)
  [ ] SDP: 生成 Answer (setup:passive, fingerprint, candidate, rtcp-mux)
  [ ] 自签 DTLS 证书 + fingerprint 计算

关键点:
  - ICE-lite 是硬门槛：浏览器不发 STUN 就直接放弃连接
  - SDP 缺字段 DTLS 握手一定失败
  - 不需要完整 ICE agent，只需 ICE-lite（服务端模式）

验收: 浏览器能完成 ICE 连接检测 + DTLS 握手
```

### Day 11-13: DTLS + SRTP

```
文件: media-svc/transport/DtlsContext.h/.cpp
      media-svc/transport/SrtpContext.h/.cpp
依赖: OpenSSL 3.0+ / libsrtp2

任务:
  [ ] DTLS: 创建 SSL_CTX (DTLS method)，加载自签证书
  [ ] DTLS: 通过 UDP 交换握手包
  [ ] DTLS: SSL_export_keying_material 导出 SRTP 密钥
  [ ] SRTP: 用 DTLS 导出的密钥初始化加密上下文
  [ ] SRTP: srtp_protect / srtp_unprotect 封装
  [ ] 每个 Peer 独立的 SRTP 上下文

验收: 和浏览器完成 DTLS 握手 → SRTP 加解密往返正确
```

### Day 14-15: RTP + RTCP 最小实现

```
新增文件:
  media-svc/rtp/RtpHeader.h               — RTP 头解析
  media-svc/rtcp/RtcpHandler.h/.cpp       — RTCP 处理

任务:
  [ ] RTP: 结构体解析（V/P/PT/SSRC/SeqNum/Timestamp/extension/padding）
  [ ] RTP: 不能硬编码 12 字节，要正确处理 CSRC 和 header extension
  [ ] RTCP: PLI (Picture Loss Indication) → 请求关键帧
  [ ] RTCP: NACK (Negative ACK) → 丢包重传（可延后）
  [ ] RTCP: SR (Sender Report) → 基础统计

验收: 能正确取出 SSRC/PT/SeqNum，响应 PLI
```

### Day 16-17: SFU 路由核心

```
文件: media-svc/sfu/Router.h/.cpp
      media-svc/sfu/Room.h/.cpp
      media-svc/sfu/Peer.h/.cpp
      media-svc/sfu/RouteTable.h/.cpp

任务:
  [ ] Router: 主循环 (收包→解密→路由→加密→转发)
  [ ] Room: 房间实体，管理 peer 列表
  [ ] Peer: SSRC/密钥/endpoint/DtlsContext/SrtpContext
  [ ] RouteTable: SSRC→Peer 映射 (O(1))

验收: 模拟两个 Peer 的 RTP 包能互相转发
```

### Day 18-19: 完善 gRPC 方法

```
文件: media-svc/grpc/MediaServiceImpl.cpp

任务:
  [ ] CreateRoom → SFU 创建房间
  [ ] AddPeer → 分配端口 + 创建 ICE/DTLS/SRTP 上下文
  [ ] SendOffer → SDP 解析 + 生成 answer
  [ ] SendIceCandidate → 添加到 Peer candidates
  [ ] RemovePeer → 清理 Peer 资源

验收: 所有 gRPC 方法实现完毕
```

### Day 20-22: Go 信令后端 + 前端 + Adapter

```
任务:
  [ ] Go REST API: 房间 CRUD + Token 签发
  [ ] Go WebSocket Hub: 连接管理 + 消息路由
  [ ] Go ↔ C++ gRPC Adapter
  [ ] 前端: index.html + room.html + webrtc.js
  [ ] 前端: setLocalDescription(offer) → WS → C++ → answer → setRemoteDescription

验收: 浏览器能完成信令交换，ICE 连接建立
```

### Day 23-28: 联调 + 修复

```
任务:
  [ ] 浏览器 A ↔ SFU ↔ 浏览器 B
  [ ] DTLS 握手调试（chrome://webrtc-internals）
  [ ] ICE 连接状态处理
  [ ] 音视频同步确认
  [ ] Bug 修复

验收: 两个 Chrome 标签页 localhost 互通音视频 ✅
```

---

## Phase 3 — RTCP 补全 + 多人 SFU ⬜

> 预计 2026-07-11 ~ 2026-07-24

| 任务 | 说明 |
|------|------|
| RTCP NACK 完善 | 丢包重传 |
| RTCP REMB/TWCC | 带宽估计（可选） |
| 多人视频网格 | 2×2 / 3×3 自适应布局 |
| 静音/关闭摄像头 | WebSocket mute + gRPC MuteTrack |
| 屏幕共享 | getDisplayMedia + 独立 video SSRC |
| 联调 | 4 人会议稳定 |

**验收**: 4 人会议 + 屏幕共享 ✅

---

## Phase 4 — AI 总结 + 文档 ⬜

> 预计 2026-07-25 ~ 2026-08-07

| 任务 | 产出 |
|------|------|
| AI Pipeline 空壳 | 不影响音视频通话 |
| whisper.cpp ASR | 实时语音转录 |
| llama.cpp 总结 | 定时/手动/终场三种模式 |
| Go Summary Service | gRPC 触发 + WebSocket 推送 |
| 前端展示 | 总结面板 UI |
| 技术文档 | 架构图 + 协议说明 |

**验收**: 5 分钟对话后生成结构化总结 ✅

---

## WebRTC 协议要点

```
浏览器 RTCPeerConnection 连接 SFU 的关键步骤：

① ICE 连通性检测
   浏览器 → STUN Binding Request → SFU
   SFU    → STUN Binding Response (XOR-MAPPED-ADDRESS) → 浏览器
   ⚠️ 这一步不做，浏览器直接放弃连接

② DTLS 握手
   双方通过 UDP 交换 DTLS 握手包
   验证 SDP 里的 fingerprint 匹配
   握手完成后双方导出 SRTP 密钥材料

③ SRTP 加密通信
   浏览器用导出的密钥加密 RTP 包
   SFU 解密 → 查 SSRC 路由 → 用目标密钥重加密 → 发出

④ RTCP 反馈
   浏览器发 PLI → SFU 转发给发送方 → 发送方产生关键帧
   浏览器发 NACK → SFU 转发 → 发送方重传丢失的包
```

---

## 风险与对策

| 风险 | 等级 | 对策 |
|------|:--:|------|
| ICE 连通失败 | 🔴 | STUN Response 格式必须正确，用 Wireshark 对照 RFC 抓包 |
| DTLS 握手超时 | 🔴 | chrome://webrtc-internals/ 查看握手状态 + SDP 字段 |
| SDP 字段缺失导致连接失败 | 🔴 | 对照浏览器 offer 逐字段确认 answer |
| 浏览器 HTTPS 限制 | 🟡 | localhost HTTP 特例可用，或 python3 http.server |
| GitHub 网络不稳定 | 🟡 | 配置代理或 SSH |
| AI 模型下载 | 🟡 | whisper tiny 75MB，提前下载 |
| 内存不足 (7.4G) | 🟢 | make -j4 限制并行数 |
