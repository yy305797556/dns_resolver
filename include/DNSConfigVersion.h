#pragma once

#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct ConfigVersion {
    std::string version;
    std::string timestamp;
    std::string author;
    std::string comment;
    nlohmann::json config;
};

class DNSConfigVersion {
public:
    explicit DNSConfigVersion(const std::string &version_dir);

    bool saveVersion(const nlohmann::json &config,
                     const std::string &author,
                     const std::string &comment);

    bool rollback(const std::string &version);
    bool rollbackToLatest();

    [[nodiscard]] std::vector<ConfigVersion> getVersionHistory() const;
    [[nodiscard]] ConfigVersion getCurrentVersion() const;

    bool compareVersions(const std::string &version1, const std::string &version2,
                         std::vector<std::string> &differences) const;

    [[nodiscard]] bool exportVersion(const std::string &version, const std::string &output_file) const;

    bool importVersion(const std::string &input_file, const std::string &comment);

private:
    std::string version_dir_;
    std::string current_version_;

    [[nodiscard]] std::string generateVersionId() const;
    [[nodiscard]] std::string getVersionPath(const std::string &version) const;
    bool loadVersion(const std::string &version, ConfigVersion &config) const;
    bool validateVersion(const ConfigVersion &version) const;
    void maintainVersionHistory(size_t max_versions = 100);
    void compareJsonObjects(const nlohmann::json &obj1, const nlohmann::json &obj2,
                            const std::string &path, std::vector<std::string> &differences) const;
};