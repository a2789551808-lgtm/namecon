#include "Config.h"
#include <yaml-cpp/yaml.h>
#include <iostream>

Config loadConfig(const std::string& path) {
    Config cfg{};
    // 默认值
    cfg.grpc_port      = 50051;
    cfg.udp_port_start = 10000;
    cfg.udp_port_end   = 20000;
    cfg.public_ip      = "127.0.0.1";

    try {
        YAML::Node root = YAML::LoadFile(path);

        if (root["server"]) {
            auto s = root["server"];
            if (s["grpc_port"]) {
                cfg.grpc_port = s["grpc_port"].as<int>();
            }
            if (s["udp_port_start"]) {
                cfg.udp_port_start = s["udp_port_start"].as<int>();
            }
            if (s["udp_port_end"]) {
                cfg.udp_port_end = s["udp_port_end"].as<int>();
            }
            if (s["public_ip"]) {
                cfg.public_ip = s["public_ip"].as<std::string>();
            }
        }

        if (root["dtls"]) {
            auto d = root["dtls"];
            if (d["cert_file"]) {
                cfg.cert_file = d["cert_file"].as<std::string>();
            }
            if (d["key_file"]) {
                cfg.key_file = d["key_file"].as<std::string>();
            }
        }

    } catch (const YAML::Exception& e) {
        std::cerr << "[config] YAML parse warning: " << e.what()
                  << " — using defaults" << std::endl;
    }

    std::cout << "[config] Loaded: grpc=" << cfg.grpc_port
              << " udp=" << cfg.udp_port_start << "-" << cfg.udp_port_end
              << " ip=" << cfg.public_ip << std::endl;

    return cfg;
}

// === ConfigMgr 单例 ===

std::shared_ptr<Config> ConfigMgr::_cfg;

void ConfigMgr::Init(const std::string& path) {
    _cfg = std::make_shared<Config>(loadConfig(path));
}

std::shared_ptr<const Config> ConfigMgr::Inst() {
    return _cfg;
}
