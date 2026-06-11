# NameCon 开发计划

> 版本: v1.0 | 日期: 2026-06-11 | 当前: Phase 1 完成

---

## 总览

| 阶段 | 目标 | 周期 | 状态 |
|------|------|:--:|:--:|
| Phase 1 | 项目骨架 + gRPC 通信 | 1 周 | ✅ 完成 |
| Phase 2 | 核心传输层 + 音视频通话 | 2~3 周 | 🔜 |
| Phase 3 | 多人会议 + 屏幕共享 | 1 周 | ⬜ |
| Phase 4 | AI 总结 + 文档 | 2 周 | ⬜ |

---

## Phase 1 — 项目骨架 ✅

> 已完成于 2026-06-09

| 任务 | 产出 | 状态 |
|------|------|:--:|
| 环境安装 | 19 项依赖全部通过 | ✅ |
| Proto 定义 | `media.proto` + gen_proto.sh 生成代码 | ✅ |
| Go 骨架编译 | signal-svc 编译通过，15MB 二进制 | ✅ |
| C++ 骨架编译 | media-svc 编译通过，700KB 二进制 | ✅ |
| gRPC 打通 | Go Client → C++ Server CreateRoom 往返成功 | ✅ |
| 构建系统 | `make build` / `make run` / `make clean` | ✅ |
| 开发规范 | `docs/rules.md` (C++ + Go) | ✅ |

**验收**: 两个二进制启动，gRPC CreateRoom 返回 room_id + token ✅

---

## Phase 2 — 核心传输层 🔜

> 预计 2026-06-11 ~ 2026-06-28

### Day 6-7: UdpServer

```
文件: media-svc/transport/UdpServer.h/.cpp
依赖: boost::asio

任务:
  [ ] 封装 boost::asio::io_context::udp::socket
  [ ] 绑定 UDP 端口 (从 Config 读取范围)
  [ ] async_receive_from 异步收包
  [ ] async_send_to 异步发包
  [ ] 管理 endpoint → peerId 映射
  [ ] 集成到 main.cpp 的 io_context

验收: 两个 UDP socket 互相收发消息
```

### Day 8-9: DtlsContext

```
文件: media-svc/transport/DtlsContext.h/.cpp
依赖: OpenSSL 3.0+

任务:
  [ ] 生成自签证书 (启动时)
  [ ] 创建 SSL_CTX (DTLS method)
  [ ] 处理 DTLS 握手包
  [ ] 导出 SRTP 密钥材料 (SSL_export_keying_material)

验收: 和浏览器完成 DTLS 握手，导出密钥
```

### Day 10-11: SrtpContext

```
文件: media-svc/transport/SrtpContext.h/.cpp
依赖: libsrtp2

任务:
  [ ] 封装 srtp_create + srtp_protect + srtp_unprotect
  [ ] 用 DTLS 导出的密钥初始化 SRTP 上下文
  [ ] 每个 Peer 独立的加密上下文

验收: 加解密往返正确
```

### Day 12: SFU 路由核心

```
文件: media-svc/sfu/Router.h/.cpp
      media-svc/sfu/Room.h/.cpp
      media-svc/sfu/Peer.h/.cpp
      media-svc/sfu/RouteTable.h/.cpp
      media-svc/sfu/Forwarder.h/.cpp

任务:
  [ ] Router: 主循环 (收包→解密→路由→加密→转发)
  [ ] Room: 房间实体，管理 peer 列表
  [ ] Peer: SSRC/密钥/endpoint 管理
  [ ] RouteTable: SSRC→Peer 映射 (O(1))
  [ ] Forwarder: RTP 包转发

验收: 模拟两个 Peer 的 SSRC 包能互相转发
```

### Day 13: 完善 gRPC 方法

```
文件: media-svc/grpc/MediaServiceImpl.cpp

任务:
  [ ] CreateRoom → SFU 创建房间
  [ ] AddPeer → 分配 UDP 端口 + 创建 DTLS/SRTP 上下文
  [ ] SendOffer → 解析 SDP + 生成 answer
  [ ] SendIceCandidate → 添加到 Peer ICE 候选列表
  [ ] RemovePeer → 清理 Peer 资源
  [ ] 错误处理完善

验收: 所有 gRPC 方法实现完毕
```

### Day 14: Go 信令后端

```
文件: signal-svc/internal/api/*
      signal-svc/internal/signaling/*
      signal-svc/internal/room/*

任务:
  [ ] REST API: 房间 CRUD + Token 签发
  [ ] WebSocket Hub: 连接管理 + 消息路由
  [ ] Room Manager: 房间生命周期

验收: curl 调用 API 创建房间，WebSocket 加入房间
```

### Day 15: Go ↔ C++ Adapter

```
文件: signal-svc/internal/sfu/adapter.go

任务:
  [ ] Go 信令操作 → gRPC 请求映射
  [ ] CreateRoom / AddPeer / SendOffer 完整调用链
  [ ] 错误处理 + 超时控制

验收: Go 信令流程完整串联 C++ SFU
```

### Day 16-18: 前端

```
文件: web/*

任务:
  [ ] index.html: 创建/加入房间 UI
  [ ] room.html: 视频网格 + 控制栏
  [ ] api.js: REST API 调用
  [ ] signaling.js: WebSocket 信令
  [ ] webrtc.js: PeerConnection 管理 (核心)
  [ ] ui.js: 界面交互

验收: 页面加载正常，WebSocket 连接成功
```

### Day 19-21: 联调

```
任务:
  [ ] 浏览器 A ↔ SFU ↔ 浏览器 B
  [ ] DTLS 握手调试
  [ ] ICE 连接状态处理
  [ ] 音视频同步确认
  [ ] Bug 修复

验收: 两个浏览器标签页互相看到听到 ✅
```

---

## Phase 3 — 多人会议 ⬜

> 预计 2026-06-29 ~ 2026-07-05

| 任务 | 产出 |
|------|------|
| 多人视频网格布局 | 2×2 / 3×3 自适应 |
| 静音/关闭摄像头 | WebSocket mute + gRPC MuteTrack |
| 屏幕共享 | getDisplayMedia + 独立 video SSRC |
| 联调 + 修复 | 4 人会议稳定 |

**验收**: 4 人会议 + 屏幕共享 ✅

---

## Phase 4 — AI 总结 + 文档 ⬜

> 预计 2026-07-06 ~ 2026-07-19

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

## 风险与对策

| 风险 | 等级 | 对策 |
|------|:--:|------|
| DTLS 握手调试困难 | 🔴 | chrome://webrtc-internals + Wireshark |
| 浏览器 HTTPS 限制 | 🟡 | localhost 特例，或 python3 http.server |
| GitHub 网络不稳定 | 🟡 | 配置代理或 SSH |
| AI 模型下载 | 🟡 | whisper tiny 75MB，提前下载 |
| 内存不足 (7.4G) | 🟢 | make -j8 限制并行数 |
