package signaling

// Hub WebSocket 连接管理中心
type Hub struct {
	// TODO: rooms map, register/unregister chan
}

func NewHub() *Hub {
	return &Hub{}
}

func (h *Hub) Run() {
	// TODO: 主循环，处理连接/断开/消息
}
