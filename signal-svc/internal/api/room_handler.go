package api

import (
	"encoding/json"
	"log/slog"
	"net/http"

	"github.com/gorilla/mux"

	"namecon/signal-svc/internal/room"
)

type APIHandler struct {
	roomMgr *room.Manager
}

func NewAPIHandler(mgr *room.Manager) *APIHandler {
	return &APIHandler{roomMgr: mgr}
}

// POST /api/rooms
func (h *APIHandler) CreateRoom(w http.ResponseWriter, r *http.Request) {
	var req struct {
		RoomName string `json:"room_name"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		req.RoomName = "会议室"
	}

	room, err := h.roomMgr.CreateRoom(req.RoomName)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
		return
	}

	writeJSON(w, http.StatusCreated, map[string]string{
		"room_id": room.ID,
		"token":   room.Token,
		"name":    room.Name,
	})
}

// POST /api/rooms/{id}/join
func (h *APIHandler) JoinRoom(w http.ResponseWriter, r *http.Request) {
	roomID := mux.Vars(r)["id"]

	var req struct {
		Username string `json:"username"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		req.Username = "匿名"
	}

	p, err := h.roomMgr.AddParticipant(roomID, req.Username)
	if err != nil {
		writeJSON(w, http.StatusNotFound, map[string]string{"error": err.Error()})
		return
	}

	// 为房间内每对参会者建立双向 Consumer（每人收其他人的音视频）
	if err := h.roomMgr.SetupConsumers(roomID); err != nil {
		slog.Error("SetupConsumers error", "error", err)
	}

	writeJSON(w, http.StatusOK, map[string]interface{}{
		"peer_id":          p.ID,
		"username":         p.Name,
		"sfu_ip":           p.SfuIP,
		"sfu_port":         p.SfuPort,
		"ice_ufrag":        p.IceUfrag,
		"ice_pwd":          p.IcePwd,
		"dtls_fingerprint": p.DtlsFingerprint,
	})
}

// GET /api/health
func (h *APIHandler) Health(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

// RegisterRoutes 注册所有 HTTP 路由
func (h *APIHandler) RegisterRoutes(r *mux.Router) {
	r.HandleFunc("/api/rooms", h.CreateRoom).Methods("POST")
	r.HandleFunc("/api/rooms/{id}/join", h.JoinRoom).Methods("POST")
	r.HandleFunc("/api/health", h.Health).Methods("GET")
}

func writeJSON(w http.ResponseWriter, status int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*") // CORS
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(data)
}
