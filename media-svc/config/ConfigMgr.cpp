#include "ConfigMgr.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include "../utils/Logger.h"

void ConfigMgr::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN("Cannot open {} - using defaults", path);
        return;
    }

    std::string line, currentSection;
    while (std::getline(file, line)) {
        // 去首尾空格和注释
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r"));
            size_t end = s.find_last_not_of(" \t\r");
            if (end != std::string::npos) s.erase(end + 1);
            // 去掉行尾注释 (# 或 ;)
            size_t comment = s.find_first_of("#;");
            if (comment != std::string::npos) s.erase(comment);
        };
        trim(line);
        if (line.empty()) continue;

        // [section]
        if (line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }

        // key = value
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        trim(key);
        trim(val);

        // 去引号
        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
                                  (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }

        if (!currentSection.empty() && !key.empty()) {
            _config_map[currentSection]._section_datas[key] = val;
        }
    }

    // 输出加载结果
    for (const auto& sec : _config_map) {
        LOG_INFO("[{}]", sec.first);
        for (const auto& kv : sec.second._section_datas) {
            LOG_INFO("  {} = {}", kv.first, kv.second);
        }
    }
}
