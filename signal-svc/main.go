package main

import (
	"fmt"
	"os"

	"namecon/signal-svc/internal/config"
	"namecon/signal-svc/internal/sfu"
	pb "namecon/signal-svc/pkg/pb/media"
)

func main() {
	fmt.Println("NameCon signal-svc starting...")

	// 加载配置
	cfg, err := config.Load("configs/signal-svc.yaml")
	if err != nil {
		fmt.Fprintf(os.Stderr, "load config: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("HTTP :%d | WS :%d | gRPC→media-svc %s:%d\n",
		cfg.Server.HTTPPort, cfg.Server.WSPort,
		cfg.MediaService.Host, cfg.MediaService.Port)

	// 验证 proto 包可用
	_ = &pb.CreateRoomReq{RoomName: "test"}
	fmt.Println("proto OK")

	// 连接 C++ media-svc
	addr := fmt.Sprintf("%s:%d", cfg.MediaService.Host, cfg.MediaService.Port)
	client, err := sfu.NewClient(addr)
	if err != nil {
		fmt.Fprintf(os.Stderr, "connect media-svc: %v\n", err)
		os.Exit(1)
	}
	defer client.Close()
	fmt.Println("gRPC connected to media-svc ✅")

	// 测试: 调用 CreateRoom
	roomID, token, err := client.CreateRoom("测试会议室")
	if err != nil {
		fmt.Fprintf(os.Stderr, "CreateRoom: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("✅ CreateRoom 成功!\n")
	fmt.Printf("   room_id: %s\n", roomID)
	fmt.Printf("   token:   %s\n", token)

	// TODO: 启动 HTTP/WS 服务
}
