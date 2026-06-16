package room

// Participant 参会者 — Go 侧管理的参会者信息
type Participant struct {
	ID               string `json:"id"`
	Name             string `json:"name"`
	RoomID           string `json:"-"`
	SfuIP            string `json:"-"`
	SfuPort          int    `json:"-"`
	IceUfrag         string `json:"-"`
	IcePwd           string `json:"-"`
	DtlsFingerprint  string `json:"-"`
}
