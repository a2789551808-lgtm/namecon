#!/bin/bash
# 生成 Go 和 C++ 的 protobuf/gRPC 代码
set -e
PROTO_DIR="$(cd "$(dirname "$0")/../proto" && pwd)"

# Go
protoc -I"$PROTO_DIR" \
  --go_out=../signal-svc/pkg/pb --go_opt=paths=source_relative \
  --go-grpc_out=../signal-svc/pkg/pb --go-grpc_opt=paths=source_relative \
  "$PROTO_DIR/media/media.proto"

# C++
protoc -I"$PROTO_DIR" \
  --cpp_out=../media-svc/generated \
  --grpc_out=../media-svc/generated \
  --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
  "$PROTO_DIR/media/media.proto"

echo "✅ Proto 代码生成完成"
