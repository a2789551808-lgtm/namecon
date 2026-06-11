# NameCon — 轻量级视频会议系统

> C++ 媒体引擎 + Go 信令服务器 + Web 前端 | 校招项目

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Go](https://img.shields.io/badge/Go-1.22+-00ADD8?logo=go)](https://go.dev/)
[![C++](https://img.shields.io/badge/C++-17-00599C?logo=c%2B%2B)](https://isocpp.org/)
[![gRPC](https://img.shields.io/badge/gRPC-1.51+-244c5a)](https://grpc.io/)

从零实现的 WebRTC SFU 视频会议系统。Go 负责信令调度与房间管理，C++ 负责 DTLS/SRTP 加解密与选择性转发（SFU），前端使用浏览器原生 WebRTC API，预留 AI 对话总结扩展位。


---

## 快速开始

```bash
# 1. 安装依赖（Ubuntu 24.04）
sudo apt install -y build-essential cmake pkg-config \
    libboost-dev libsrtp2-dev libspdlog-dev \
    libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc \
    golang-go nodejs npm docker.io

go install google.golang.org/protobuf/cmd/protoc-gen-go@latest
go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest

# 2. 构建
make build

# 3. 启动
./scripts/dev.sh

# 4. 打开浏览器
# http://localhost:3000
```

> 详细环境配置见 [docs/development.md](docs/development.md)

---

## 架构

```
浏览器 (WebRTC) ──WebSocket──▶ Go 信令服务 ──gRPC──▶ C++ SFU 媒体引擎
       │                                                 │
       └──────────── SRTP/UDP (加密音视频) ───────────────┘
```

| 服务 | 语言 | 端口 | 职责 |
|------|------|------|------|
| **signal-svc** | Go | :8080 HTTP / :8081 WS | REST API、WebSocket 信令、房间管理 |
| **media-svc** | C++ | :50051 gRPC / UDP 10000-20000 | DTLS 握手、SRTP 加解密、SFU 路由转发 |
| **web** | HTML/JS | :3000 | 浏览器原生 WebRTC，零外部依赖 |

---

## 项目结构

```
namecon/
├── README.md
├── Makefile
├── docker-compose.yml
├── proto/media/              # gRPC 协议定义 (Go & C++ 共享)
├── configs/                  # 服务配置文件
├── scripts/                  # 构建 & 开发脚本
├── deploy/                   # Docker 部署文件
├── web/                      # 前端 (原生 HTML/JS)
├── signal-svc/               # Go 信令服务
├── media-svc/                # C++ 媒体引擎
└── docs/                     # 文档
    ├── development.md        # 开发流程
    ├── project-overview.md   # 项目详细概览
    ├── architecture.md       # 架构设计
    ├── protocol.md           # 信令协议
    └── interview.md          # 面试准备
```

---

## 技术栈

| 层 | 技术 |
|----|------|
| 前端 | 浏览器原生 WebRTC API、WebSocket |
| Go HTTP/WS | `gorilla/mux`、`gorilla/websocket` |
| Go gRPC | `google.golang.org/grpc` |
| C++ 网络 | `boost::asio` (异步 UDP) |
| C++ DTLS | OpenSSL 3.0+ |
| C++ SRTP | libsrtp 2.5+ |
| C++ gRPC | gRPC C++ 1.51+ |
| C++ 日志 | `spdlog` |
| 构建 | CMake 3.20+ / Go Module |
| 部署 | Docker + Docker Compose |

---

## 文档索引

| 文档 | 说明 |
|------|------|
| [docs/development.md](docs/development.md) | 开发流程、环境配置、分阶段计划 |
| [docs/project-overview.md](docs/project-overview.md) | 项目详细概览：架构、协议、SFU 核心、AI 预留 |
| [docs/architecture.md](docs/architecture.md) | 架构设计文档 |
| [docs/protocol.md](docs/protocol.md) | 信令协议详细说明 |
| [docs/interview.md](docs/interview.md) | 面试准备要点 |

---

## 开发阶段

```
Phase 1 — MVP 音视频通话 (2~3 周)
  ✅ 两人浏览器互通

Phase 2 — 多人会议 (1 周)
  多人网格 + 静音 + 屏幕共享

Phase 3 — AI 总结 (2 周)
  whisper.cpp ASR + llama.cpp 对话总结

Phase 4 — 文档 (1 周)
  架构图 + 面试话术
```

---

## License

MIT
