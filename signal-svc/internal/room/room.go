package room

import (
	"sync"
	"time"
)

// Room 房间实体
type Room struct {
	ID           string
	Name         string
	Token        string
	Participants map[string]*Participant
	mu           sync.RWMutex
	createdAt    time.Time
}
