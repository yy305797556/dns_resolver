#include "DNSCachePersistor.h"
#include "DNSUtils.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using Milliseconds = std::chrono::milliseconds;
using TimePoint = std::chrono::system_clock::time_point;

constexpr const char *CACHE_FORMAT_VERSION = "1.0";
constexpr int64_t MAX_CACHE_AGE = 24 * 60 * 60 * 1000;// 1 day in milliseconds
constexpr const char *CACHE_FIELD_NAME_VERSION = "version";
constexpr const char *CACHE_FIELD_NAME_TIMESTAMP = "timestamp";
constexpr const char *CACHE_FIELD_NAME_RECORDS = "records";

constexpr const char *CACHE_RECORDS_FIELD_NAME_HOSTNAME = "hostname";
constexpr const char *CACHE_RECORDS_FIELD_NAME_IP = "ip_addresses";
constexpr const char *CACHE_RECORDS_FIELD_NAME_EXPIRE_TIME = "expire_time";
constexpr const char *CACHE_RECORDS_FIELD_NAME_IS_VALID = "is_valid";

bool DNSCachePersistor::save(const DNSCache &cache, const std::string &filename) {
    try {
        nlohmann::json j;
        j[CACHE_FIELD_NAME_VERSION] = CACHE_FORMAT_VERSION;
        j[CACHE_FIELD_NAME_TIMESTAMP] = DNSUtils::getTime();

        nlohmann::json records = nlohmann::json::array();
        cache.forEach([&records](const std::string &hostname,
                                 const DNSRecord &record) {
            if (record.is_valid) {
                nlohmann::json recordJson = serializeRecord(record);
                recordJson[CACHE_RECORDS_FIELD_NAME_HOSTNAME] = hostname;
                records.push_back(recordJson);
            }
        });

        j[CACHE_FIELD_NAME_RECORDS] = records;

        std::ofstream file(filename);
        if (!file) {
            return false;
        }
        file << j.dump(4);
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Error saving cache: " << e.what() << std::endl;
        return false;
    }
}

bool DNSCachePersistor::load(DNSCache &cache, const std::string &filename) {
    try {
        std::ifstream file(filename);
        if (!file) {
            return false;
        }

        nlohmann::json cache_data;
        file >> cache_data;

        // 验证版本
        if (!cache_data.contains(CACHE_FIELD_NAME_VERSION) ||
            cache_data[CACHE_FIELD_NAME_VERSION] != CACHE_FORMAT_VERSION) {
            throw std::runtime_error("Invalid cache format version");
        }

        // 验证时间戳
        if (!cache_data.contains(CACHE_FIELD_NAME_TIMESTAMP)) {
            throw std::runtime_error("Missing cache timestamp");
        }

        // 检查缓存是否过期
        auto cache_time = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(cache_data[CACHE_FIELD_NAME_TIMESTAMP]));
        auto now = std::chrono::system_clock::now();
        if ((now - cache_time).count() > MAX_CACHE_AGE) {
            std::cerr << "Cache file is too old, ignoring" << std::endl;
            return false;
        }

        // 加载缓存条目
        if (!cache_data.contains(CACHE_FIELD_NAME_RECORDS) ||
            !cache_data[CACHE_FIELD_NAME_RECORDS].is_array()) {
            throw std::runtime_error("Invalid cache records format");
        }

        for (const auto &recordJson: cache_data[CACHE_FIELD_NAME_RECORDS]) {
            DNSRecord record = deserializeRecord(recordJson);

            // 只加载未过期的记录
            if (record.is_valid && record.expire_time > now) {
                cache.update(record.hostname, record.ip_addresses);
            }
        }

        return true;
    } catch (const std::exception &e) {
        std::cerr << "Error loading cache: " << e.what() << std::endl;
        return false;
    }
}

nlohmann::json DNSCachePersistor::serializeRecord(const DNSRecord &record) {
    nlohmann::json j;
    j[CACHE_RECORDS_FIELD_NAME_HOSTNAME] = record.hostname;
    j[CACHE_RECORDS_FIELD_NAME_IP] = record.ip_addresses;
    j[CACHE_RECORDS_FIELD_NAME_EXPIRE_TIME] =
            std::chrono::duration_cast<std::chrono::seconds>(record.expire_time.time_since_epoch()).count();
    j[CACHE_RECORDS_FIELD_NAME_IS_VALID] = record.is_valid;
    return j;
}

DNSRecord DNSCachePersistor::deserializeRecord(const nlohmann::json &j) {

    // 验证必需字段
    if (!j.contains(CACHE_RECORDS_FIELD_NAME_HOSTNAME) ||
        !j.contains(CACHE_RECORDS_FIELD_NAME_IP) ||
        !j.contains(CACHE_RECORDS_FIELD_NAME_EXPIRE_TIME) ||
        !j.contains(CACHE_RECORDS_FIELD_NAME_IS_VALID)) {
        return {};// 跳过无效条目
    }

    DNSRecord record;
    record.hostname = j[CACHE_RECORDS_FIELD_NAME_HOSTNAME];
    record.ip_addresses = j[CACHE_RECORDS_FIELD_NAME_IP].get<std::vector<std::string>>();
    record.expire_time = std::chrono::system_clock::from_time_t(j[CACHE_RECORDS_FIELD_NAME_EXPIRE_TIME].get<int64_t>());
    record.is_valid = j[CACHE_RECORDS_FIELD_NAME_IS_VALID].get<bool>();
    return record;
}

std::string DNSCachePersistor::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&tt), "%Y-%m-%d %H:%M:%S UTC");
    return ss.str();
}

bool DNSCachePersistor::isValidCache(const std::string &filename) {
    try {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            return false;
        }
        nlohmann::json cache_data;
        file >> cache_data;
        // 检查基本结构
        if (!cache_data.contains(CACHE_FIELD_NAME_VERSION) ||
            !cache_data.contains(CACHE_FIELD_NAME_TIMESTAMP) ||
            !cache_data.contains(CACHE_FIELD_NAME_RECORDS)) {
            return false;
        }
        // 验证版本
        if (cache_data[CACHE_FIELD_NAME_VERSION] != CACHE_FORMAT_VERSION) {
            return false;
        }

        // 检查时间戳
        auto cache_time = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(cache_data[CACHE_FIELD_NAME_TIMESTAMP]));
        auto now = std::chrono::system_clock::now();
        if ((now - cache_time).count() > MAX_CACHE_AGE) {
            return false;
        }

        return true;
    } catch (const std::exception &) {
        return false;
    }
}