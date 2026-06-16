package signaling

// Message WebSocket 消息格式
type Message struct {
	Type    string      `json:"type"`
	Payload interface{} `json:"payload,omitempty"`
}
