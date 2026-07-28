#pragma once
#include <spdlog/spdlog.h>

// 日志宏：内部调用 spdlog 全局 logger，自带文件名+行号
#define LOG_DEBUG(fmt, ...) SPDLOG_DEBUG(fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  SPDLOG_INFO(fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  SPDLOG_WARN(fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) SPDLOG_ERROR(fmt, ##__VA_ARGS__)

namespace Logger {
    // 从 ConfigMgr 的 [log] 段读取配置，初始化 spdlog 全局 logger
    // configPath: ini 配置文件路径
    void init(const std::string& configPath);
}
