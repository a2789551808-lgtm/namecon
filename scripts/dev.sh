#!/bin/bash
# 开发环境一键启动
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

cleanup() {
    echo ""
    echo "[dev] Shutting down..."
    kill $CPP_PID $GO_PID $WEB_PID 2>/dev/null
    wait $CPP_PID $GO_PID $WEB_PID 2>/dev/null
    echo "[dev] All services stopped"
}
trap cleanup EXIT INT TERM

echo "╔══════════════════════════════════════════╗"
echo "║   NameCon — 开发环境启动                   ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# 1. C++ media-svc
echo "=== 启动 C++ media-svc (:50051) ==="
"$ROOT/media-svc/build/media-svc" "$ROOT/configs/media-svc.yaml" &
CPP_PID=$!
sleep 1

# 2. Go signal-svc
echo "=== 启动 Go signal-svc (:8080) ==="
cd "$ROOT"
"$ROOT/build/signal-svc" &
GO_PID=$!
sleep 1

# 3. 前端
echo "=== 启动前端 (:3000) ==="
cd "$ROOT/web"
python3 -m http.server 3000 &
WEB_PID=$!

echo ""
echo "══════════════════════════════════════════"
echo "  media-svc  →  localhost:50051 (gRPC)"
echo "  signal-svc →  localhost:8080  (HTTP)"
echo "  web        →  localhost:3000  (HTML)"
echo "══════════════════════════════════════════"
echo "  Ctrl+C 停止所有服务"
echo ""

wait
