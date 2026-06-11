#pragma once
#include <string>
#include <map>
#include "../utils/Singleton.h"

// 配置段信息 — 一个 section 下的所有 key-value 对
struct SectionInfo {
    std::map<std::string, std::string> _section_datas;

    std::string operator[](const std::string& key) const {
        auto it = _section_datas.find(key);
        if (it == _section_datas.end()) {
            return "";
        }
        return it->second;
    }
};

// 配置管理器单例 — 从 YAML 加载，按 section/key 访问
class ConfigMgr : public Singleton<ConfigMgr> {
    friend class Singleton<ConfigMgr>;

public:
    void Load(const std::string& path);

    SectionInfo operator[](const std::string& section) const {
        auto it = _config_map.find(section);
        if (it == _config_map.end()) {
            return SectionInfo();
        }
        return it->second;
    }

private:
    ConfigMgr()  = default;
    ~ConfigMgr() = default;
    std::map<std::string, SectionInfo> _config_map;
};
