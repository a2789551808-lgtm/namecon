#include "Logger.h"
#include "../config/ConfigMgr.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <vector>

namespace Logger {

void init(const std::string& configPath) {
    // 加载配置
    ConfigMgr::GetInstance().Load(configPath);
    auto logCfg = ConfigMgr::GetInstance()["log"];

    std::string logFile = logCfg["log_file"];
    // 去引号
    if (logFile.size() >= 2 && logFile.front() == '"' && logFile.back() == '"')
        logFile = logFile.substr(1, logFile.size() - 2);

    std::string levelStr = logCfg["log_level"];
    if (levelStr.empty()) levelStr = "info";
    if (levelStr.size() >= 2 && levelStr.front() == '"' && levelStr.back() == '"')
        levelStr = levelStr.substr(1, levelStr.size() - 2);

    int maxSize = 10, maxBackups = 5, maxAge = 30;
    try {
        if (!logCfg["log_max_size_mb"].empty())
            maxSize = std::stoi(logCfg["log_max_size_mb"]);
        if (!logCfg["log_max_backups"].empty())
            maxBackups = std::stoi(logCfg["log_max_backups"]);
        if (!logCfg["log_max_age_days"].empty())
            maxAge = std::stoi(logCfg["log_max_age_days"]);
    } catch (...) {}

    // 创建 sink 列表
    std::vector<spdlog::sink_ptr> sinks;
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v");
    sinks.push_back(consoleSink);

    if (!logFile.empty()) {
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFile, maxSize * 1024 * 1024, maxBackups);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%s:%#] %v");
        sinks.push_back(fileSink);
    }

    // 创建 multi-sink logger
    auto logger = std::make_shared<spdlog::logger>("media-svc", sinks.begin(), sinks.end());

    // 设置级别
    if (levelStr == "debug")        logger->set_level(spdlog::level::debug);
    else if (levelStr == "info")    logger->set_level(spdlog::level::info);
    else if (levelStr == "warn")    logger->set_level(spdlog::level::warn);
    else if (levelStr == "error")   logger->set_level(spdlog::level::err);
    else                            logger->set_level(spdlog::level::info);

    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
}

} // namespace Logger
