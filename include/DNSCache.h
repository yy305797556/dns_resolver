#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

struct DNSRecord {
    std::string hostname{};
    std::vector<std::string> ip_addresses{};
    std::chrono::system_clock::time_point expire_time{};
    bool is_valid{};
};

class DNSCache {
public:
    explicit DNSCache(std::chrono::seconds ttl = std::chrono::seconds(300));

    void update(const std::string &hostname, const std::vector<std::string> &ips);

    bool get(const std::string &hostname, std::vector<std::string> &ips);

    void remove(const std::string &hostname);
    void clear();

    // 遍历缓存的方法
    void forEach(std::function<void(const std::string &, const DNSRecord &)> fn) const;

    // 获取缓存统计信息
    size_t size() const;
    size_t capacity() const;
    double hit_rate() const;

private:
    std::unordered_map<std::string, DNSRecord> cache_;
    mutable std::mutex mutex_;
    std::chrono::seconds ttl_;
    size_t max_size_;

    // 统计信息
    std::atomic<size_t> hits_{0};
    std::atomic<size_t> misses_{0};

    void cleanup();
};