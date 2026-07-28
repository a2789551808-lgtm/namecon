package config

import (
	"bufio"
	"os"
	"strconv"
	"strings"
)

type Config struct {
	Server ServerConfig
	Log    LogConfig
	Media  MediaConfig
}

type ServerConfig struct {
	HTTPPort string
	WSPort   string
	CertFile string
	KeyFile  string
}

type LogConfig struct {
	File       string
	MaxSizeMB  int
	MaxBackups int
	MaxAgeDays int
	Level      string
}

type MediaConfig struct {
	Host string
	Port string
}

func Load(path string) (*Config, error) {
	cfg := &Config{
		Log: LogConfig{
			MaxSizeMB:  10,
			MaxBackups: 5,
			MaxAgeDays: 30,
			Level:      "info",
		},
	}

	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	sections := map[string]map[string]string{}
	var currentSection string

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, ";") || strings.HasPrefix(line, "#") {
			continue
		}
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			currentSection = line[1 : len(line)-1]
			sections[currentSection] = map[string]string{}
			continue
		}
		eq := strings.Index(line, "=")
		if eq < 0 || currentSection == "" {
			continue
		}
		key := strings.TrimSpace(line[:eq])
		val := strings.TrimSpace(line[eq+1:])
		if len(val) >= 2 && ((val[0] == '"' && val[len(val)-1] == '"') ||
			(val[0] == '\'' && val[len(val)-1] == '\'')) {
			val = val[1 : len(val)-1]
		}
		sections[currentSection][key] = val
	}

	if s, ok := sections["server"]; ok {
		cfg.Server.HTTPPort = s["http_port"]
		cfg.Server.WSPort = s["ws_port"]
		cfg.Server.CertFile = s["cert_file"]
		cfg.Server.KeyFile = s["key_file"]
	}

	if s, ok := sections["log"]; ok {
		cfg.Log.File = s["log_file"]
		if v, err := strconv.Atoi(s["log_max_size_mb"]); err == nil {
			cfg.Log.MaxSizeMB = v
		}
		if v, err := strconv.Atoi(s["log_max_backups"]); err == nil {
			cfg.Log.MaxBackups = v
		}
		if v, err := strconv.Atoi(s["log_max_age_days"]); err == nil {
			cfg.Log.MaxAgeDays = v
		}
		if s["log_level"] != "" {
			cfg.Log.Level = s["log_level"]
		}
	}

	if s, ok := sections["media_service"]; ok {
		cfg.Media.Host = s["host"]
		cfg.Media.Port = s["port"]
	}

	return cfg, scanner.Err()
}
