package main

import (
	"fmt"
	"log"
	"log/slog"
	"os"

	"namecon/signal-svc/internal/config"
	"namecon/signal-svc/internal/core"
	"namecon/signal-svc/internal/logger"
)

func main() {
	configPath := "configs/signal-svc.ini"
	if len(os.Args) > 1 {
		configPath = os.Args[1]
	}

	cfg, err := config.Load(configPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fatal: load config: %v\n", err)
		os.Exit(1)
	}
	logger.Init(cfg.Log)
	slog.Info("signal-svc starting", "http_port", cfg.Server.HTTPPort)

	server, err := core.New(configPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fatal: %v\n", err)
		os.Exit(1)
	}
	defer server.Close()

	log.Fatal(server.Run())
}
