#pragma once
#include <string>
#include <memory>

struct Config {
    int         grpc_port;
    int         udp_port_start;
    int         udp_port_end;
    std::string public_ip;
    std::string cert_file;
    std::string key_file;
};

Config loadConfig(const std::string& path);

// 单例管理器 — 加载一次，全局只读
class ConfigMgr {
public:
    static void Init(const std::string& path);
    static std::shared_ptr<const Config> Inst();

private:
    static std::shared_ptr<Config> _cfg;
};
