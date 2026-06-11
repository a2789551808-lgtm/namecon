package sfu

import (
	"context"
	"fmt"
	"time"

	pb "namecon/signal-svc/pkg/pb/media"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

// Client media-svc 的 gRPC 客户端
type Client struct {
	conn  *grpc.ClientConn
	media pb.MediaServiceClient
}

// NewClient 创建并连接 media-svc
func NewClient(addr string) (*Client, error) {
	conn, err := grpc.NewClient(addr,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
	)
	if err != nil {
		return nil, fmt.Errorf("dial %s: %w", addr, err)
	}

	return &Client{
		conn:  conn,
		media: pb.NewMediaServiceClient(conn),
	}, nil
}

// Close 关闭连接
func (c *Client) Close() error {
	return c.conn.Close()
}

// CreateRoom 调用 C++ 创建房间
func (c *Client) CreateRoom(name string) (roomID, token string, err error) {
    ctx, cancel := context.WithTimeout(                //  2 秒超时
        context.Background(), 2*time.Second,
    )
    defer cancel()                                     //  函数退出时取消，防止泄漏

    resp, err := c.media.CreateRoom(ctx,               //  调远程方法！
        &pb.CreateRoomReq{RoomName: name},             //  传入请求参数
    )
    if err != nil {
        return "", "", fmt.Errorf("CreateRoom: %w", err)
    }
    return resp.GetRoomId(), resp.GetToken(), nil      //  返回结果
}
