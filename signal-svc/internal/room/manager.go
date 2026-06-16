package room

import (
	"fmt"
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
		fmt.Printf("[room] C++ CreateRoom failed (non-fatal): %v\n", err)
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
		ID:               peerID,
		Name:             username,
		RoomID:           roomID,
		SfuIP:            sfuIP,
		SfuPort:          int(sfuPort),
		IceUfrag:         ufrag,
		IcePwd:           pwd,
		DtlsFingerprint:  fp,
	}

	room.mu.Lock()
	room.Participants[peerID] = p
	room.mu.Unlock()

	return p, nil
}

// RemoveParticipant 移除参会者 → 调 C++ RemovePeer
func (m *Manager) RemoveParticipant(roomID, peerID string) error {
	room, err := m.GetRoom(roomID)
	if err != nil {
		return err
	}

	room.mu.Lock()
	delete(room.Participants, peerID)
	room.mu.Unlock()

	return m.sfu.RemovePeer(peerID)
}

// SetupForwarding 设置房间内所有参会者互相转发
func (m *Manager) SetupForwarding(roomID string) error {
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

	// 每个人 ↔ 其他人
	for _, from := range peers {
		for _, to := range peers {
			if from.ID == to.ID {
				continue
			}
			if err := m.sfu.AddForwarding(from.ID, to.ID); err != nil {
				return fmt.Errorf("AddForwarding %s→%s: %w", from.ID, to.ID, err)
			}
		}
	}
	return nil
}
