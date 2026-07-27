# NameCon — 轻量级视频会议系统

> C++ 媒体引擎 + Go 信令服务器 + Web 前端

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Go](https://img.shields.io/badge/Go-1.25+-00ADD8?logo=go)](https://go.dev/)
[![C++](https://img.shields.io/badge/C++-17-00599C?logo=c%2B%2B)](https://isocpp.org/)
[![gRPC](https://img.shields.io/badge/gRPC-1.51+-244c5a)](https://grpc.io/)

从零实现的 WebRTC SFU 视频会议系统。Go 负责信令调度与房间管理，C++ 负责 ICE/DTLS/SRTP 与选择性转发（SFU），前端使用浏览器原生 WebRTC API。

> **多人会议已支持**：Consumer 模型 + SSRC 改写 + Unified Plan，详见 [docs/2026-07-27-multi-user-sfu-refactor.md](docs/2026-07-27-multi-user-sfu-refactor.md)


## 快速开始

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

# 3. 启动
./media-svc/build/media-svc &    # C++ SFU
./build/signal-svc &             # Go 信令

# 4. 浏览器打开 localhost:8080，创建房间即可测试
```

### Docker 部署（云服务器）

```bash
# 修改 configs/media-svc.ini 的 public_ip 为服务器公网 IP
# 或设置环境变量（推荐）：
PUBLIC_IP=1.2.3.4 docker compose up -d --build

# 腾讯云等国内服务器需先配镜像加速：
sudo mkdir -p /etc/docker
echo '{"registry-mirrors":["https://mirror.ccs.tencentyun.com"]}' | sudo tee /etc/docker/daemon.json
sudo systemctl restart docker
```

---

## 架构

```
浏览器 (WebRTC) ──WebSocket──▶ Go 信令服务 ──gRPC──▶ C++ SFU 媒体引擎
       │                                                 │
       └──────────── SRTP/UDP (加密音视频) ───────────────┘
```

| 服务 | 语言 | 端口 | 职责 |
|------|------|------|------|
| **signal-svc** | Go | :8080 | REST API、WebSocket 信令、房间管理、前端静态文件 |
| **media-svc** | C++ | :50051 gRPC / UDP 10000 | ICE-lite、DTLS 握手、SRTP 加解密、SFU 路由转发（Consumer 模型 + SSRC 改写 + RTCP Terminator） |
| **web** | HTML/JS | — | 浏览器原生 WebRTC，Go 直接托管 |

---

## 项目结构

```
namecon/
├── README.md
├── Makefile
├── docker-compose.yml
├── proto/media/              # gRPC 协议定义 (Go & C++ 共享)
├── configs/                  # 服务配置文件
│   ├── media-svc.ini         #   public_ip = SFU 公网地址
│   └── signal-svc.ini        #   host = media-svc (Docker) / 127.0.0.1 (本地)
├── scripts/                  # 构建脚本
├── deploy/                   # Docker 部署文件
│   ├── Dockerfile.media-svc
│   ├── Dockerfile.signal-svc
│   └── docker-compose.prod.yml
├── web/                      # 前端 (原生 HTML/JS)
├── signal-svc/               # Go 信令服务
└── media-svc/                # C++ 媒体引擎
    ├── transport/            #   UDP、ICE、DTLS、SRTP、PacketRouter
    ├── sfu/                  #   Router、RouteTable、Peer、Consumer、Producer
    ├── sdp/                  #   SDP 解析与生成（Unified Plan 多 m=）
    ├── rtcp/                 #   RTCP 解析（翻译在 Router）
    ├── rtp/                  #   RTP 头解析 + writeFixedHeader
    ├── grpc/                 #   gRPC 服务实现
    └── core/                 #   启动、配置
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
| C++ CRC32 | zlib (`crc32()`) |
| C++ 配置 | 手写 INI 解析 |
| C++ 日志 | `spdlog`（链接但未使用，实际用 std::cout） |
| 构建 | CMake 3.20+ / Go Module |
| 部署 | Docker + Docker Compose |

---

## License

MIT
