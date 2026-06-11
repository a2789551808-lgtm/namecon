#!/bin/bash
# 生成 Go 和 C++ 的 protobuf/gRPC 代码
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROTO_DIR="$ROOT/proto"

echo "=== 生成 Go protobuf 代码 ==="
protoc -I"$PROTO_DIR" \
  --go_out="$ROOT/signal-svc/pkg/pb" --go_opt=paths=source_relative \
  --go-grpc_out="$ROOT/signal-svc/pkg/pb" --go-grpc_opt=paths=source_relative \
  "$PROTO_DIR/media/media.proto"

echo "=== 生成 C++ protobuf 代码 ==="
protoc -I"$PROTO_DIR" \
  --cpp_out="$ROOT/media-svc/generated" \
  --grpc_out="$ROOT/media-svc/generated" \
  --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
  "$PROTO_DIR/media/media.proto"

echo "✅ Proto 代码生成完成"
ls -la "$ROOT/signal-svc/pkg/pb/media/" 2>/dev/null || echo "(Go 生成目录)"
ls -la "$ROOT/media-svc/generated/media/" 2>/dev/null || ls -la "$ROOT/media-svc/generated/" 2>/dev/null || echo "(C++ 生成目录)"
