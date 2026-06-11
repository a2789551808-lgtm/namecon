#!/bin/bash
# 开发环境一键启动
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "=== 启动 C++ media-svc ==="
cd "$ROOT/media-svc/build"
./media-svc "$ROOT/configs/media-svc.yaml" &

echo "=== 启动 Go signal-svc ==="
cd "$ROOT/signal-svc"
go run main.go --config "$ROOT/configs/signal-svc.yaml" &

echo "=== 启动前端 ==="
cd "$ROOT/web"
python3 -m http.server 3000 &
wait
