package room

import (
	"fmt"
	"log/slog"
	"math/rand"
	"sync"
	"time"

	"namecon/signal-svc/internal/sfu"
)

// Manager 房间生命周期管理（内存存储）
type Manager struct {
	mu    sync.RWMutex
	rooms map[string]*Room
	sfu   *sfu.Client
}

func NewManager(sfuClient *sfu.Client) *Manager {
	return &Manager{
		rooms: make(map[string]*Room),
		sfu:   sfuClient,
	}
}

// CreateRoom 创建房间 — Go 侧生成房号
func (m *Manager) CreateRoom(name string) (*Room, error) {
	// Go 侧生成 6 位随机房号
	roomID := fmt.Sprintf("%06d", rand.Intn(900000)+100000)

	// 通知 C++ 侧（可选：预留用于后续扩展）
	_, _, err := m.sfu.CreateRoom(name)
	if err != nil {
		// C++ 调用失败不阻止创建，仅日志
		slog.Warn("C++ CreateRoom failed (non-fatal)", "error", err)
	}

	room := &Room{
		ID:           roomID,
		Name:         name,
		Token:        "token_" + roomID,
		Participants: make(map[string]*Participant),
		createdAt:    time.Now(),
	}

	m.mu.Lock()
	m.rooms[roomID] = room
	m.mu.Unlock()

	return room, nil
}

// GetRoom 按 roomId 查找房间
func (m *Manager) GetRoom(roomID string) (*Room, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	room, ok := m.rooms[roomID]
	if !ok {
		return nil, fmt.Errorf("room not found: %s", roomID)
	}
	return room, nil
}

// AddParticipant 参会者加入 → 调 C++ AddPeer → 拿到 SFU 连接参数
func (m *Manager) AddParticipant(roomID, username string) (*Participant, error) {
	room, err := m.GetRoom(roomID)
	if err != nil {
		return nil, err
	}

	peerID := fmt.Sprintf("peer_%d", rand.Intn(99999))
	sfuIP, sfuPort, ufrag, pwd, fp, err := m.sfu.AddPeer(peerID)
	if err != nil {
		return nil, fmt.Errorf("sfu AddPeer: %w", err)
	}

	p := &Participant{
		ID:              peerID,
		Name:            username,
		RoomID:          roomID,
		SfuIP:           sfuIP,
		SfuPort:         int(sfuPort),
		IceUfrag:        ufrag,
		IcePwd:          pwd,
		DtlsFingerprint: fp,
	}

	room.mu.Lock()
	room.Participants[peerID] = p
	room.mu.Unlock()

	return p, nil
}

// RemoveParticipant 移除参会者 -> 调 C++ RemovePeer -> 房间空了自动回收
func (m *Manager) RemoveParticipant(roomID, peerID string) error {
	room, err := m.GetRoom(roomID)
	if err != nil {
		return err
	}

	room.mu.Lock()
	delete(room.Participants, peerID)
	empty := len(room.Participants) == 0
	room.mu.Unlock()

	if empty {
		m.mu.Lock()
		delete(m.rooms, roomID)
		m.mu.Unlock()
		slog.Info("room destroyed (empty)", "room_id", roomID)
	}

	return m.sfu.RemovePeer(peerID)
}

// SetupConsumers 为房间内每对参会者建立双向 Consumer（每人收其他人的音视频）
// 调用时机：新参与者加入后（AddPeer 完成、通知 renegotiate 之前）
func (m *Manager) SetupConsumers(roomID string) error {
	room, err := m.GetRoom(roomID)
	if err != nil {
		return err
	}

	room.mu.RLock()
	defer room.mu.RUnlock()

	peers := make([]*Participant, 0, len(room.Participants))
	for _, p := range room.Participants {
		peers = append(peers, p)
	}

	// 双向：每个人订阅其他人的 video + audio
	for _, sub := range peers {
		for _, pub := range peers {
			if sub.ID == pub.ID {
				continue
			}
			// sub 收 pub 的 video
			if _, err := m.sfu.AddConsumer(sub.ID, pub.ID, true); err != nil {
				return fmt.Errorf("AddConsumer %s<-%s(video): %w", sub.ID, pub.ID, err)
			}
			// sub 收 pub 的 audio
			if _, err := m.sfu.AddConsumer(sub.ID, pub.ID, false); err != nil {
				return fmt.Errorf("AddConsumer %s<-%s(audio): %w", sub.ID, pub.ID, err)
			}
		}
	}
	return nil
}
