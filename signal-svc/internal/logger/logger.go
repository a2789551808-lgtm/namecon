package logger

import (
	"io"
	"log/slog"
	"os"
	"strings"

	"namecon/signal-svc/internal/config"

	"gopkg.in/natefinch/lumberjack.v2"
)

func Init(cfg config.LogConfig) {
	var w io.Writer = os.Stdout

	if cfg.File != "" {
		lj := &lumberjack.Logger{
			Filename:   cfg.File,
			MaxSize:    cfg.MaxSizeMB,
			MaxBackups: cfg.MaxBackups,
			MaxAge:     cfg.MaxAgeDays,
			Compress:   true,
		}
		w = io.MultiWriter(os.Stdout, lj)
	}

	var level slog.Level
	switch strings.ToLower(cfg.Level) {
	case "debug":
		level = slog.LevelDebug
	case "warn":
		level = slog.LevelWarn
	case "error":
		level = slog.LevelError
	default:
		level = slog.LevelInfo
	}

	handler := slog.NewTextHandler(w, &slog.HandlerOptions{Level: level})
	slog.SetDefault(slog.New(handler))
}
