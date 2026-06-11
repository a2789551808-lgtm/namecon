package signaling

// Message WebSocket 消息类型
type Message struct {
	Type    string      `json:"type"`
	Payload interface{} `json:"payload"`
}

// TODO: 消息路由分发
