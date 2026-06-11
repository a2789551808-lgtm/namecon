#include "ConfigMgr.h"
#include <yaml-cpp/yaml.h>
#include <iostream>

void ConfigMgr::Load(const std::string& path) {
    try {
        YAML::Node root = YAML::LoadFile(path);

        // 遍历所有 section，将 key-value 全部转为字符串存入 _config_map
        for (const auto& section_pair : root) {
            const std::string& section_name = section_pair.first.as<std::string>();
            const YAML::Node& section_node  = section_pair.second;

            SectionInfo sectionInfo;
            if (section_node.IsMap()) {
                for (const auto& kv : section_node) {
                    std::string key = kv.first.as<std::string>();
                    std::string val = kv.second.as<std::string>();
                    sectionInfo._section_datas[key] = val;
                }
            }
            _config_map[section_name] = sectionInfo;
        }
    } catch (const YAML::Exception& e) {
        std::cerr << "[config] YAML parse warning: " << e.what()
                  << " — using defaults" << std::endl;
    }

    // 输出加载结果
    for (const auto& sec : _config_map) {
        std::cout << "[" << sec.first << "]" << std::endl;
        for (const auto& kv : sec.second._section_datas) {
            std::cout << "  " << kv.first << " = " << kv.second << std::endl;
        }
    }
}
