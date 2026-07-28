<div align="center">

# 🎥 NameCon

### Lightweight WebRTC Video Conferencing System

**C++ Media Engine · Go Signaling Server · Web Frontend**

[![License](https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square)](LICENSE)
[![Go](https://img.shields.io/badge/Go-1.25+-00ADD8?style=flat-square&logo=go&logoColor=white)](https://go.dev/)
[![C++](https://img.shields.io/badge/C++-17-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)](https://isocpp.org/)
[![gRPC](https://img.shields.io/badge/gRPC-1.81+-244c5a?style=flat-square&logo=grpc)](https://grpc.io/)
[![WebRTC](https://img.shields.io/badge/WebRTC-SFU-333333?style=flat-square&logo=webrtc&logoColor=white)](https://webrtc.org/)
[![Docker](https://img.shields.io/badge/Docker-Ready-2496ED?style=flat-square&logo=docker&logoColor=white)](https://www.docker.com/)

</div>

---

> 🚀 A from-scratch WebRTC SFU video conferencing system. Go handles signaling & room management, C++ handles ICE/DTLS/SRTP and Selective Forwarding (SFU), and the frontend uses browser-native WebRTC APIs.
>
> ✅ **Multi-party conferencing supported** — Consumer model + SSRC rewriting + Unified Plan. See [refactor doc](docs/2026-07-27-multi-user-sfu-refactor.md) & [debug log](docs/2026-07-27-multi-user-debug-log.md).

---

## 📋 Table of Contents

- [Quick Start](#-quick-start)
- [Architecture](#-architecture)
- [Project Structure](#-project-structure)
- [Tech Stack](#-tech-stack)
- [License](#-license)

---

## 🚀 Quick Start

### Local Development (Ubuntu 24.04 / WSL2)

```bash
# 1. Install dependencies
sudo apt install -y build-essential cmake pkg-config \
    libboost-dev libsrtp2-dev libspdlog-dev \
    libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc \
    libssl-dev zlib1g-dev golang-go

go install google.golang.org/protobuf/cmd/protoc-gen-go@latest
go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest

# 2. Build
export GOTOOLCHAIN=auto  # Auto-download Go toolchain
make build

# 3. Start services
./media-svc/build/media-svc &    # C++ SFU
./build/signal-svc &             # Go signaling

# 4. Open localhost:8080 in browser, create a room and test!
```

### Docker Deployment (Cloud Server)

```bash
# Set your server's public IP
PUBLIC_IP=1.2.3.4 docker compose up -d --build

# Logs are persisted to host via ./logs:/var/log/namecon volume

# For China cloud servers (Tencent/Alibaba), configure mirror first:
sudo mkdir -p /etc/docker
echo '{"registry-mirrors":["https://mirror.ccs.tencentyun.com"]}' | sudo tee /etc/docker/daemon.json
sudo systemctl restart docker
```

---

## 🏗 Architecture

```
Browser (WebRTC) ──WebSocket──▶ Go Signaling ──gRPC──▶ C++ SFU Engine
       │                                                    │
       └──────────── SRTP/UDP (encrypted A/V) ──────────────┘
```

| Service | Language | Port | Responsibility |
|---------|----------|------|----------------|
| **signal-svc** | Go | :8080 | REST API, WebSocket signaling, room management, static file hosting |
| **media-svc** | C++ | :50051 gRPC / UDP 10000 | ICE-lite, DTLS handshake, SRTP encrypt/decrypt, SFU routing (Consumer model + SSRC rewrite + RTCP Terminator) |
| **web** | HTML/JS | — | Browser-native WebRTC, served by Go |

---

## 📁 Project Structure

```
namecon/
├── proto/media/              # gRPC protocol definitions (shared Go & C++)
├── configs/                  # Service config files
│   ├── media-svc.ini         #   public_ip + [log] logging config
│   └── signal-svc.ini        #   host + [log] logging config
├── scripts/                  # Build scripts
├── deploy/                   # Docker deployment
│   ├── Dockerfile.media-svc
│   ├── Dockerfile.signal-svc
│   └── docker-compose.prod.yml
├── web/                      # Frontend (vanilla HTML/JS)
├── signal-svc/               # Go signaling server
│   └── internal/
│       ├── api/              #   REST handlers
│       ├── config/           #   INI config parser
│       ├── core/             #   Server bootstrap
│       ├── logger/           #   slog + lumberjack logger
│       ├── room/             #   Room manager
│       ├── signaling/        #   WebSocket hub
│       └── sfu/              #   gRPC client to C++
├── media-svc/                # C++ media engine
│   ├── transport/            #   UDP, ICE, DTLS, SRTP, PacketRouter
│   ├── sfu/                  #   Router, RouteTable, Peer, Consumer, Producer
│   ├── sdp/                  #   SDP parse & generate (Unified Plan)
│   ├── rtcp/                 #   RTCP parsing (translation in Router)
│   ├── rtp/                  #   RTP header parse + writeFixedHeader
│   ├── grpc/                 #   gRPC service impl
│   ├── config/               #   INI config manager
│   ├── utils/                #   Logger (spdlog), Stats
│   └── core/                 #   Bootstrap, MediaServer
└── docs/                     # Design docs, debug logs, specs
```

---

## 🛠 Tech Stack

| Layer | Technology |
|-------|-----------|
| Frontend | Browser-native WebRTC API, WebSocket |
| Go HTTP/WS | `gorilla/mux`, `gorilla/websocket` |
| Go gRPC | `google.golang.org/grpc` |
| Go Logging | `log/slog` + `lumberjack` (rotating file + console, `[log]` config) |
| C++ Networking | `boost::asio` (async UDP) |
| C++ DTLS | OpenSSL 3.0+ |
| C++ SRTP | libsrtp 2.5+ |
| C++ gRPC | gRPC C++ 1.51+ |
| C++ Logging | `spdlog` 1.12+ (console + rotating file, `LOG_*` macros, `[log]` config) |
| C++ Config | Custom INI parser |
| Build | CMake 3.20+ / Go Module |
| Deploy | Docker + Docker Compose |

---

## 📄 License

MIT

---

<br>

---

<div align="center">

# 🎥 NameCon

### 轻量级 WebRTC 视频会议系统

**C++ 媒体引擎 · Go 信令服务 · Web 前端**

</div>

---

> 🚀 从零实现的 WebRTC SFU 视频会议系统。Go 负责信令调度与房间管理，C++ 负责 ICE/DTLS/SRTP 与选择性转发（SFU），前端使用浏览器原生 WebRTC API。
>
> ✅ **多人会议已支持**：Consumer 模型 + SSRC 改写 + Unified Plan，详见 [重构文档](docs/2026-07-27-multi-user-sfu-refactor.md) 和 [调试记录](docs/2026-07-27-multi-user-debug-log.md)。

---

## 📋 目录

- [快速开始](#-快速开始)
- [架构](#-架构)
- [项目结构](#-项目结构)
- [技术栈](#-技术栈)
- [License](#-license-1)

---

## 🚀 快速开始

### 本地开发（Ubuntu 24.04 / WSL2）

```bash
# 1. 安装依赖
sudo apt install -y build-essential cmake pkg-config \
    libboost-dev libsrtp2-dev libspdlog-dev \
    libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc \
    libssl-dev zlib1g-dev golang-go

go install google.golang.org/protobuf/cmd/protoc-gen-go@latest
go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest

# 2. 编译
export GOTOOLCHAIN=auto  # Go 自动下载合适工具链
make build

# 3. 启动服务
./media-svc/build/media-svc &    # C++ SFU
./build/signal-svc &             # Go 信令

# 4. 浏览器打开 localhost:8080，创建房间即可测试
```

### Docker 部署（云服务器）

```bash
# 设置服务器公网 IP
PUBLIC_IP=1.2.3.4 docker compose up -d --build

# 日志通过 ./logs:/var/log/namecon 卷持久化到宿主机

# 腾讯云等国内服务器需先配镜像加速：
sudo mkdir -p /etc/docker
echo '{"registry-mirrors":["https://mirror.ccs.tencentyun.com"]}' | sudo tee /etc/docker/daemon.json
sudo systemctl restart docker
```

---

## 🏗 架构

```
浏览器 (WebRTC) ──WebSocket──▶ Go 信令服务 ──gRPC──▶ C++ SFU 媒体引擎
       │                                                 │
       └──────────── SRTP/UDP (加密音视频) ───────────────┘
```

| 服务 | 语言 | 端口 | 职责 |
|------|------|------|------|
| **signal-svc** | Go | :8080 | REST API、WebSocket 信令、房间管理、前端静态文件 |
| **media-svc** | C++ | :50051 gRPC / UDP 10000 | ICE-lite、DTLS 握手、SRTP 加解密、SFU 路由转发（Consumer 模型 + SSRC 改写 + RTCP Terminator） |
| **web** | HTML/JS | - | 浏览器原生 WebRTC，Go 直接托管 |

---

## 📁 项目结构

```
namecon/
├── proto/media/              # gRPC 协议定义（Go & C++ 共享）
├── configs/                  # 服务配置文件
│   ├── media-svc.ini         #   public_ip + [log] 日志配置
│   └── signal-svc.ini        #   host + [log] 日志配置
├── scripts/                  # 构建脚本
├── deploy/                   # Docker 部署文件
├── web/                      # 前端（原生 HTML/JS）
├── signal-svc/               # Go 信令服务
│   └── internal/
│       ├── api/              #   REST 处理器
│       ├── config/           #   INI 配置解析
│       ├── core/             #   服务启动
│       ├── logger/           #   slog + lumberjack 日志
│       ├── room/             #   房间管理
│       ├── signaling/        #   WebSocket Hub
│       └── sfu/              #   gRPC 客户端
├── media-svc/                # C++ 媒体引擎
│   ├── transport/            #   UDP、ICE、DTLS、SRTP、PacketRouter
│   ├── sfu/                  #   Router、RouteTable、Peer、Consumer、Producer
│   ├── sdp/                  #   SDP 解析与生成（Unified Plan 多 m=）
│   ├── rtcp/                 #   RTCP 解析（翻译在 Router）
│   ├── rtp/                  #   RTP 头解析 + writeFixedHeader
│   ├── grpc/                 #   gRPC 服务实现
│   ├── config/               #   INI 配置管理
│   ├── utils/                #   Logger (spdlog)、Stats
│   └── core/                 #   启动、配置
└── docs/                     # 设计文档、调试记录、规格说明
```

---

## 🛠 技术栈

| 层 | 技术 |
|----|------|
| 前端 | 浏览器原生 WebRTC API、WebSocket |
| Go HTTP/WS | `gorilla/mux`、`gorilla/websocket` |
| Go gRPC | `google.golang.org/grpc` |
| Go 日志 | `log/slog` + `lumberjack`（rotating file + 控制台，`[log]` 配置段） |
| C++ 网络 | `boost::asio`（异步 UDP） |
| C++ DTLS | OpenSSL 3.0+ |
| C++ SRTP | libsrtp 2.5+ |
| C++ gRPC | gRPC C++ 1.51+ |
| C++ 日志 | `spdlog` 1.12+（控制台 + rotating file，`LOG_*` 宏，`[log]` 配置段） |
| C++ 配置 | 手写 INI 解析 |
| 构建 | CMake 3.20+ / Go Module |
| 部署 | Docker + Docker Compose |

---

## 📄 License

MIT
