package config

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"
)

type Config struct {
	Server       ServerConfig
	JWT          JWTConfig
	MediaService MediaServiceConfig
}

type ServerConfig struct {
	HTTPPort int
	WSPort   int
	CertFile string
	KeyFile  string
}

type JWTConfig struct {
	Secret      string
	ExpireHours int
}

type MediaServiceConfig struct {
	Host string
	Port int
}

func Load(path string) (*Config, error) {
	cfg := &Config{
		Server:       ServerConfig{HTTPPort: 8080, WSPort: 8081},
		JWT:          JWTConfig{Secret: "change-me", ExpireHours: 24},
		MediaService: MediaServiceConfig{Host: "127.0.0.1", Port: 50051},
	}

	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	var section string
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") || strings.HasPrefix(line, ";") {
			continue
		}
		// [section]
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			section = line[1 : len(line)-1]
			continue
		}
		// key = value
		eq := strings.Index(line, "=")
		if eq < 0 {
			continue
		}
		key := strings.TrimSpace(line[:eq])
		val := strings.TrimSpace(line[eq+1:])
		// 去引号
		val = strings.Trim(val, `"'`)

		switch section {
		case "server":
			switch key {
			case "http_port":
				cfg.Server.HTTPPort, _ = strconv.Atoi(val)
			case "ws_port":
				cfg.Server.WSPort, _ = strconv.Atoi(val)
			case "cert_file":
				cfg.Server.CertFile = val
			case "key_file":
				cfg.Server.KeyFile = val
			}
		case "jwt":
			switch key {
			case "secret":
				cfg.JWT.Secret = val
			case "expire_hours":
				cfg.JWT.ExpireHours, _ = strconv.Atoi(val)
			}
		case "media_service":
			switch key {
			case "host":
				cfg.MediaService.Host = val
			case "port":
				cfg.MediaService.Port, _ = strconv.Atoi(val)
			}
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("read config: %w", err)
	}
	return cfg, nil
}
