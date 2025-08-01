#pragma once

#include "DNSCache.h"
#include <nlohmann/json.hpp>
#include <string>

class DNSCachePersistor {
public:
    static bool save(const DNSCache &cache, const std::string &filename);
    static bool load(DNSCache &cache, const std::string &filename);
    static bool backup(const DNSCache &cache, const std::string &backup_dir);
    static bool restore(DNSCache &cache, const std::string &backup_file);
    static std::vector<std::string> listBackups(const std::string &backup_dir);
    static bool compactCache(DNSCache &cache);
    static bool isValidCache(const std::string &filename);

    struct CacheStats {
        size_t total_entries;
        size_t valid_entries;
        size_t expired_entries;
        std::chrono::system_clock::time_point oldest_entry;
        std::chrono::system_clock::time_point newest_entry;
        size_t file_size;
    };

    static CacheStats analyzeCache(const std::string &filename);

private:
    static nlohmann::json serializeRecord(const DNSRecord &record);
    static DNSRecord deserializeRecord(const nlohmann::json &j);
    static std::string getCurrentTimestamp();
};