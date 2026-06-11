.PHONY: proto build run clean

# 生成 protobuf 代码
proto:
	./scripts/gen_proto.sh

# 编译所有模块
build:
	./scripts/build.sh

# 开发环境一键启动
run:
	./scripts/dev.sh

# 清理编译产物
clean:
	rm -rf media-svc/build signal-svc/build build
