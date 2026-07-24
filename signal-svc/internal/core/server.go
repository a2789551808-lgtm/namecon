package core

import (
	"fmt"
	"log"
	"net/http"

	"github.com/gorilla/mux"

	"namecon/signal-svc/internal/api"
	"namecon/signal-svc/internal/config"
	"namecon/signal-svc/internal/room"
	"namecon/signal-svc/internal/sfu"
	"namecon/signal-svc/internal/signaling"
)

// Server 信令服务总控 — C++ 的 MediaServer 对标物
type Server struct {
	cfg       *config.Config
	sfuClient *sfu.Client
	roomMgr   *room.Manager
	hub       *signaling.Hub
	router    *mux.Router
}

// New 创建并连接所有组件
func New(configPath string) (*Server, error) {
	cfg, err := config.Load(configPath)
	if err != nil {
		return nil, fmt.Errorf("load config: %w", err)
	}

	// gRPC → C++
	sfuAddr := fmt.Sprintf("%s:%d", cfg.MediaService.Host, cfg.MediaService.Port)
	sfuClient, err := sfu.NewClient(sfuAddr)
	if err != nil {
		return nil, fmt.Errorf("connect media-svc: %w", err)
	}
	log.Println("gRPC connected to media-svc ✅")

	// Room 管理器
	roomMgr := room.NewManager(sfuClient)

	// HTTP 路由
	r := mux.NewRouter()
	api.NewAPIHandler(roomMgr).RegisterRoutes(r)

	// WebSocket
	hub := signaling.NewHub(roomMgr, sfuClient)
	r.HandleFunc("/ws", hub.ServeWS)

	// 前端静态文件
	r.PathPrefix("/").Handler(http.FileServer(http.Dir("web")))

	return &Server{
		cfg:       cfg,
		sfuClient: sfuClient,
		roomMgr:   roomMgr,
		hub:       hub,
		router:    r,
	}, nil
}

// Run 启动 HTTP/HTTPS 服务（阻塞）
func (s *Server) Run() error {
	addr := fmt.Sprintf(":%d", s.cfg.Server.HTTPPort)

	if s.cfg.Server.CertFile != "" && s.cfg.Server.KeyFile != "" {
		fmt.Printf("NameCon signal-svc starting (HTTPS) on %s\n", addr)
		return http.ListenAndServeTLS(addr, s.cfg.Server.CertFile, s.cfg.Server.KeyFile, s.router)
	}

	fmt.Printf("NameCon signal-svc starting (HTTP) on %s\n", addr)
	return http.ListenAndServe(addr, s.router)
}

// Close 清理资源
func (s *Server) Close() {
	if s.sfuClient != nil {
		s.sfuClient.Close()
	}
}
