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

// CreateRoom 创建房间
func (c *Client) CreateRoom(name string) (roomID, token string, err error) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	resp, err := c.media.CreateRoom(ctx, &pb.CreateRoomReq{RoomName: name})
	if err != nil {
		return "", "", fmt.Errorf("CreateRoom: %w", err)
	}
	return resp.GetRoomId(), resp.GetToken(), nil
}

// AddPeer 注册参会者, 返回 SFU 连接参数 (ICE + DTLS)
func (c *Client) AddPeer(peerID string) (sfuIP string, sfuPort int32,
	iceUfrag, icePwd, dtlsFingerprint string, err error) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	resp, err := c.media.AddPeer(ctx, &pb.AddPeerReq{PeerId: peerID})
	if err != nil {
		return "", 0, "", "", "", fmt.Errorf("AddPeer: %w", err)
	}
	return resp.GetSfuIp(), resp.GetSfuPort(),
		resp.GetIceUfrag(), resp.GetIcePwd(),
		resp.GetDtlsFingerprint(), nil
}

// RecvMid 本地结构：mid → publisher 映射（与 proto RecvMid 对应）
type RecvMid struct {
	Mid             string
	PublisherPeerID string
	IsVideo         bool
}

// SendOffer 发送浏览器 SDP Offer + recv_mids，获取 SFU Answer
func (c *Client) SendOffer(peerID, sdp string, recvMids []RecvMid) (answerSDP string, err error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	req := &pb.SendOfferReq{
		PeerId: peerID,
		Sdp:    sdp,
	}
	for _, rm := range recvMids {
		req.RecvMids = append(req.RecvMids, &pb.RecvMid{
			Mid:             rm.Mid,
			PublisherPeerId: rm.PublisherPeerID,
			IsVideo:         rm.IsVideo,
		})
	}

	resp, err := c.media.SendOffer(ctx, req)
	if err != nil {
		return "", fmt.Errorf("SendOffer: %w", err)
	}
	return resp.GetAnswerSdp(), nil
}

// AddConsumer 为 subscriber 创建订阅 publisher 的 Consumer，返回 SFU 分配的出口 SSRC
func (c *Client) AddConsumer(subscriberPeerID, publisherPeerID string, isVideo bool) (uint32, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	resp, err := c.media.AddConsumer(ctx, &pb.AddConsumerReq{
		SubscriberPeerId: subscriberPeerID,
		PublisherPeerId:  publisherPeerID,
		IsVideo:          isVideo,
	})
	if err != nil {
		return 0, fmt.Errorf("AddConsumer: %w", err)
	}
	return resp.GetRewrittenSsrc(), nil
}

// RemoveConsumer 取消订阅（当前 C++ 实现为占位，真实清理在 RemovePeer）
func (c *Client) RemoveConsumer(subscriberPeerID, publisherPeerID string, isVideo bool) error {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	_, err := c.media.RemoveConsumer(ctx, &pb.RemoveConsumerReq{
		SubscriberPeerId: subscriberPeerID,
		PublisherPeerId:  publisherPeerID,
		IsVideo:          isVideo,
	})
	if err != nil {
		return fmt.Errorf("RemoveConsumer: %w", err)
	}
	return nil
}

// RemovePeer 移除参会者
func (c *Client) RemovePeer(peerID string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	_, err := c.media.RemovePeer(ctx, &pb.RemovePeerReq{PeerId: peerID})
	if err != nil {
		return fmt.Errorf("RemovePeer: %w", err)
	}
	return nil
}
