package config

import (
	"os"

	"gopkg.in/yaml.v3"
)

// Config 信令服务配置
type Config struct {
	Server       ServerConfig       `yaml:"server"`
	JWT          JWTConfig          `yaml:"jwt"`
	MediaService MediaServiceConfig `yaml:"media_service"`
}

type ServerConfig struct {
	HTTPPort int `yaml:"http_port"`
	WSPort   int `yaml:"ws_port"`
}

type JWTConfig struct {
	Secret      string `yaml:"secret"`
	ExpireHours int    `yaml:"expire_hours"`
}

type MediaServiceConfig struct {
	Host string `yaml:"host"`
	Port int    `yaml:"port"`
}

func Load(path string) (*Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	cfg := &Config{
		Server: ServerConfig{
			HTTPPort: 8080,
			WSPort:   8081,
		},
		JWT: JWTConfig{
			Secret:      "change-me",
			ExpireHours: 24,
		},
		MediaService: MediaServiceConfig{
			Host: "127.0.0.1",
			Port: 50051,
		},
	}
	if err := yaml.Unmarshal(data, cfg); err != nil {
		return nil, err
	}
	return cfg, nil
}
