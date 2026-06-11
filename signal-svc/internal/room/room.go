package room

// Room 房间实体
type Room struct {
	ID           string
	Name         string
	Participants map[string]*Participant
}
