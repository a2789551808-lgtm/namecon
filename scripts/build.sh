#!/bin/bash
# 编译所有模块
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "=== 编译 C++ media-svc ==="
cd "$ROOT/media-svc"
mkdir -p build && cd build
cmake .. && make -j$(nproc)

echo "=== 编译 Go signal-svc ==="
cd "$ROOT/signal-svc"
go build -o ../build/signal-svc .

echo "✅ 编译完成"
