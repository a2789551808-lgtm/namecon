#pragma once
#include <string>

struct Config {
    int grpc_port = 50051;
    int udp_port_start = 10000;
    int udp_port_end = 20000;
    std::string public_ip = "127.0.0.1";
    std::string cert_file;
    std::string key_file;
};

Config loadConfig(const std::string& path);
