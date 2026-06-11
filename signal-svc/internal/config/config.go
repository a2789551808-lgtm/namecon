package config

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
	// TODO: 从 YAML 文件加载配置
	return &Config{}, nil
}
