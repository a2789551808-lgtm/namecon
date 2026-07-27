package signaling

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sync"

	"github.com/gorilla/websocket"

	"namecon/signal-svc/internal/room"
	"namecon/signal-svc/internal/sfu"
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true }, // localhost 允许所有
}

// Hub WebSocket 连接管理中心
type Hub struct {
	roomMgr *room.Manager
	sfu     *sfu.Client
	mu      sync.RWMutex
	clients map[*Client]bool // 所有连接
}

func NewHub(mgr *room.Manager, sfuClient *sfu.Client) *Hub {
	return &Hub{
		roomMgr: mgr,
		sfu:     sfuClient,
		clients: make(map[*Client]bool),
	}
}

// ServeWS HTTP 升级到 WebSocket
func (h *Hub) ServeWS(w http.ResponseWriter, r *http.Request) {
	defer func() {
		if err := recover(); err != nil {
			log.Printf("[WS] ServeWS panic recovered: %v", err)
		}
	}()

	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("[WS] Upgrade error: %v", err)
		return
	}

	client := &Client{
		hub:  h,
		conn: conn,
		send: make(chan []byte, 32),
	}

	h.mu.Lock()
	h.clients[client] = true
	h.mu.Unlock()

	go client.writePump()
	go client.readPump()
}

// 发给房间内所有人（除 sender 自己）
func (h *Hub) broadcastToRoom(roomID string, msg []byte, sender *Client) {
	h.mu.RLock()
	defer h.mu.RUnlock()
	for c := range h.clients {
		if c.roomID == roomID && c != sender {
			select {
			case c.send <- msg:
			default:
			}
		}
	}
}

// 发给指定客户端
func (h *Hub) sendToClient(client *Client, msg interface{}) {
	data, _ := json.Marshal(msg)
	select {
	case client.send <- data:
	default:
	}
}

// 处理客户端消息
func (h *Hub) handleMessage(client *Client, raw []byte) {
	var msg Message
	if err := json.Unmarshal(raw, &msg); err != nil {
		h.sendToClient(client, map[string]string{"type": "error", "message": "invalid json"})
		return
	}

	switch msg.Type {
	case "join":
		h.handleJoin(client, msg)
	case "offer":
		h.handleOffer(client, msg)
	case "leave":
		h.handleLeave(client)
	default:
		log.Printf("[WS] Unknown message type: %s", msg.Type)
	}
}

func (h *Hub) handleJoin(client *Client, msg Message) {
	payload, _ := json.Marshal(msg.Payload)
	var join struct {
		RoomID   string `json:"room_id"`
		Username string `json:"username"`
	}
	json.Unmarshal(payload, &join)

	// 调 room manager 获取 SFU 参数
	p, err := h.roomMgr.AddParticipant(join.RoomID, join.Username)
	if err != nil {
		h.sendToClient(client, map[string]string{"type": "error", "message": err.Error()})
		return
	}

	client.peerID = p.ID
	client.roomID = join.RoomID
	client.username = join.Username

	// 收集房间内已有参与者（通知新加入者）
	existingPeers := []map[string]string{}
	h.mu.RLock()
	for c := range h.clients {
		if c.roomID == join.RoomID && c != client && c.peerID != "" {
			existingPeers = append(existingPeers, map[string]string{
				"peer_id":  c.peerID,
				"username": c.username,
			})
		}
	}
	h.mu.RUnlock()

	// 告诉客户端: 连接就绪 + 已有参与者
	h.sendToClient(client, map[string]interface{}{
		"type":             "joined",
		"peer_id":          p.ID,
		"sfu_ip":           p.SfuIP,
		"sfu_port":         p.SfuPort,
		"ice_ufrag":        p.IceUfrag,
		"ice_pwd":          p.IcePwd,
		"dtls_fingerprint": p.DtlsFingerprint,
		"existing_peers":   existingPeers,
	})

	// 通知房间内其他人：需要重协商（新增 recvonly transceiver）
	notify, _ := json.Marshal(map[string]string{
		"type":     "renegotiate",
		"peer_id":  p.ID,
		"username": join.Username,
		"action":   "join",
	})
	h.broadcastToRoom(join.RoomID, notify, client)

	// 为房间内每对参会者建立双向 Consumer（B 收 A，A 收 B）
	if err := h.roomMgr.SetupConsumers(join.RoomID); err != nil {
		log.Printf("[WS] SetupConsumers error: %v", err)
	}

	log.Printf("[WS] %s joined room %s as %s", join.Username, join.RoomID, p.ID)
}

func (h *Hub) handleOffer(client *Client, msg Message) {
	payload, _ := json.Marshal(msg.Payload)
	var offer struct {
		SDP      string `json:"sdp"`
		RecvMids []struct {
			Mid             string `json:"mid"`
			PublisherPeerID string `json:"publisher_peer_id"`
			IsVideo         bool   `json:"is_video"`
		} `json:"recv_mids"`
	}
	json.Unmarshal(payload, &offer)

	// 转 sfu.RecvMid
	var recvMids []sfu.RecvMid
	for _, rm := range offer.RecvMids {
		recvMids = append(recvMids, sfu.RecvMid{
			Mid:             rm.Mid,
			PublisherPeerID: rm.PublisherPeerID,
			IsVideo:         rm.IsVideo,
		})
	}

	// 调 C++ 生成 answer
	answerSDP, err := h.sfu.SendOffer(client.peerID, offer.SDP, recvMids)
	if err != nil {
		h.sendToClient(client, map[string]string{"type": "error", "message": fmt.Sprintf("SendOffer: %v", err)})
		return
	}

	h.sendToClient(client, map[string]string{
		"type": "answer",
		"sdp":  answerSDP,
	})

	log.Printf("[WS] Offer → Answer for %s (%d bytes, %d recv_mids)",
		client.peerID, len(answerSDP), len(recvMids))
}

func (h *Hub) handleLeave(client *Client) {
	defer func() {
		if r := recover(); r != nil {
			log.Printf("[WS] handleLeave panic: %v", r)
		}
	}()

	if client.roomID != "" && client.peerID != "" {
		if err := h.roomMgr.RemoveParticipant(client.roomID, client.peerID); err != nil {
			log.Printf("[WS] RemoveParticipant error: %v", err)
		}
		// 通知房间内其他人：有人离开，移除对应 video
		leaveNotify, _ := json.Marshal(map[string]string{
			"type":    "renegotiate",
			"peer_id": client.peerID,
			"action":  "leave",
		})
		h.broadcastToRoom(client.roomID, leaveNotify, nil)
	}

	h.mu.Lock()
	delete(h.clients, client)
	h.mu.Unlock()

	// 安全关闭 send channel
	func() {
		defer func() { recover() }()
		close(client.send)
	}()
}
