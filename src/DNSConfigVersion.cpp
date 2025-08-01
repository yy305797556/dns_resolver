
#include "DNSConfigVersion.h"

#include "DNSUtils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

DNSConfigVersion::DNSConfigVersion(const std::string &version_dir)
    : version_dir_(version_dir) {

    // 创建版本目录（如果不存在）
    std::filesystem::create_directories(version_dir_);

    // 获取当前版本（最新版本）
    auto versions = getVersionHistory();
    if (!versions.empty()) {
        current_version_ = versions.back().version;
    }
}

bool DNSConfigVersion::saveVersion(const nlohmann::json &config, const std::string &author,
                                   const std::string &comment) {
    try {
        ConfigVersion version;
        version.version = generateVersionId();
        version.timestamp = DNSUtils::getTime();
        version.author = author;
        version.comment = comment;
        version.config = config;

        // 验证版本
        if (!validateVersion(version)) {
            return false;
        }

        // 保存版本文件
        std::string version_path = getVersionPath(version.version);
        std::ofstream file(version_path);
        if (!file) {
            return false;
        }

        nlohmann::json j = {
                {"version", version.version},
                {"timestamp", version.timestamp},
                {"author", version.author},
                {"comment", version.comment},
                {"config", version.config}};

        file << std::setw(4) << j << std::endl;

        // 更新当前版本
        current_version_ = version.version;

        // 维护版本历史
        maintainVersionHistory();

        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool DNSConfigVersion::rollback(const std::string &version) {
    try {
        ConfigVersion target_version;
        if (!loadVersion(version, target_version)) {
            return false;
        }

        // 创建回滚版本
        ConfigVersion rollback_version;
        rollback_version.version = generateVersionId();
        rollback_version.timestamp = DNSUtils::getTime();
        rollback_version.author = "leigod";// 使用当前用户
        rollback_version.comment = "Rollback to version " + version;
        rollback_version.config = target_version.config;

        // 保存回滚版本
        if (!saveVersion(rollback_version.config,
                         rollback_version.author,
                         rollback_version.comment)) {
            return false;
        }

        current_version_ = rollback_version.version;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool DNSConfigVersion::rollbackToLatest() {
    auto versions = getVersionHistory();
    if (versions.empty()) {
        return false;
    }
    return rollback(versions.back().version);
}

std::vector<ConfigVersion> DNSConfigVersion::getVersionHistory() const {
    std::vector<ConfigVersion> versions;

    try {
        for (const auto &entry:
             std::filesystem::directory_iterator(version_dir_)) {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".json") {

                ConfigVersion version;
                if (loadVersion(
                            entry.path().stem().string(), version)) {
                    versions.push_back(version);
                }
            }
        }

        // 按时间戳排序
        std::sort(versions.begin(), versions.end(),
                  [](const ConfigVersion &a, const ConfigVersion &b) {
                      return a.timestamp < b.timestamp;
                  });

    } catch (const std::exception &) {
        // 处理文件系统错误
    }

    return versions;
}

ConfigVersion DNSConfigVersion::getCurrentVersion() const {
    ConfigVersion version;
    if (!current_version_.empty()) {
        loadVersion(current_version_, version);
    }
    return version;
}

bool DNSConfigVersion::compareVersions(
        const std::string &version1,
        const std::string &version2,
        std::vector<std::string> &differences) const {

    try {
        ConfigVersion v1, v2;
        if (!loadVersion(version1, v1) || !loadVersion(version2, v2)) {
            return false;
        }

        differences.clear();
        compareJsonObjects(v1.config, v2.config, "", differences);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool DNSConfigVersion::exportVersion(
        const std::string &version,
        const std::string &output_file) const {

    try {
        ConfigVersion v;
        if (!loadVersion(version, v)) {
            return false;
        }

        std::ofstream file(output_file);
        if (!file) {
            return false;
        }

        file << std::setw(4) << v.config << std::endl;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool DNSConfigVersion::importVersion(const std::string &input_file, const std::string &comment) {

    try {
        std::ifstream file(input_file);
        if (!file) {
            return false;
        }

        nlohmann::json config;
        file >> config;

        return saveVersion(config, "leigod", comment);
    } catch (const std::exception &) {
        return false;
    }
}

// Private helper methods
std::string DNSConfigVersion::generateVersionId() const {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch())
                          .count();

    std::stringstream ss;
    ss << std::hex << now_ms;
    return ss.str();
}

std::string DNSConfigVersion::getVersionPath(
        const std::string &version) const {
    return version_dir_ + "/" + version + ".json";
}

bool DNSConfigVersion::loadVersion(
        const std::string &version,
        ConfigVersion &config) const {

    try {
        std::string version_path = getVersionPath(version);
        std::ifstream file(version_path);
        if (!file) {
            return false;
        }

        nlohmann::json j;
        file >> j;

        config.version = j["version"];
        config.timestamp = j["timestamp"];
        config.author = j["author"];
        config.comment = j["comment"];
        config.config = j["config"];

        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool DNSConfigVersion::validateVersion(
        const ConfigVersion &version) const {

    // 检查必填字段
    if (version.version.empty() ||
        version.timestamp.empty() ||
        version.author.empty()) {
        return false;
    }

    // 检查配置内容
    if (version.config.is_null() || version.config.empty()) {
        return false;
    }

    // 验证基本配置结构
    try {
        const auto &servers = version.config["servers"];
        const auto &cache = version.config["cache"];
        const auto &retry = version.config["retry"];
        const auto &metrics = version.config["metrics"];
        const auto &global = version.config["global"];

        if (!servers.is_array() ||
            !cache.is_object() ||
            !retry.is_object() ||
            !metrics.is_object() ||
            !global.is_object()) {
            return false;
        }
    } catch (const nlohmann::json::exception &) {
        return false;
    }

    return true;
}

void DNSConfigVersion::maintainVersionHistory(size_t max_versions) {
    try {
        std::vector<std::filesystem::directory_entry> versions;

        // 收集所有版本文件
        for (const auto &entry:
             std::filesystem::directory_iterator(version_dir_)) {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".json") {
                versions.push_back(entry);
            }
        }

        // 按修改时间排序
        std::sort(versions.begin(), versions.end(),
                  [](const auto &a, const auto &b) {
                      return a.last_write_time() < b.last_write_time();
                  });

        // 删除超出限制的旧版本
        while (versions.size() > max_versions) {
            std::filesystem::remove(versions.front().path());
            versions.erase(versions.begin());
        }
    } catch (const std::exception &) {
        // 处理文件系统错误
    }
}

void DNSConfigVersion::compareJsonObjects(const nlohmann::json &obj1, const nlohmann::json &obj2,
                                          const std::string &path, std::vector<std::string> &differences) const {

    if (obj1.type() != obj2.type()) {
        differences.push_back(path + ": Type mismatch");
        return;
    }

    if (obj1.is_object()) {
        for (auto it = obj1.begin(); it != obj1.end(); ++it) {
            std::string new_path = path.empty() ? it.key() : path + "." + it.key();

            if (obj2.find(it.key()) == obj2.end()) {
                differences.push_back(
                        new_path + ": Key removed in second version");
                continue;
            }

            compareJsonObjects(it.value(), obj2[it.key()], new_path, differences);
        }

        for (auto it = obj2.begin(); it != obj2.end(); ++it) {
            if (obj1.find(it.key()) == obj1.end()) {
                std::string new_path = path.empty() ? it.key() : path + "." + it.key();
                differences.push_back(
                        new_path + ": Key added in second version");
            }
        }
    } else if (obj1.is_array()) {
        if (obj1.size() != obj2.size()) {
            differences.push_back(
                    path + ": Array size mismatch (" +
                    std::to_string(obj1.size()) + " vs " +
                    std::to_string(obj2.size()) + ")");
        }

        for (size_t i = 0; i < std::min(obj1.size(), obj2.size()); ++i) {
            compareJsonObjects(obj1[i], obj2[i],
                               path + "[" + std::to_string(i) + "]",
                               differences);
        }
    } else if (obj1 != obj2) {
        differences.push_back(
                path + ": Value changed from '" + obj1.dump() +
                "' to '" + obj2.dump() + "'");
    }
}