#include "DNSCache.h"

#include <algorithm>

DNSCache::DNSCache(std::chrono::seconds ttl)
    : ttl_(ttl), max_size_(10000) {
    cleanup();
}

void DNSCache::update(const std::string &hostname,
                      const std::vector<std::string> &ips) {
    std::lock_guard<std::mutex> lock(mutex_);
    cleanup();
    if (cache_.size() >= max_size_) {
        // 如果缓存已满，移除最早过期的记录
        auto oldest = std::ranges::min_element(cache_,
                                               [](const auto &a, const auto &b) {
                                                   return a.second.expire_time < b.second.expire_time;
                                               });
        if (oldest != cache_.end()) {
            cache_.erase(oldest);
        } else {
            // 如果找不到最早过期的记录，移除第一个记录
            if (!cache_.empty()) {
                cache_.erase(cache_.begin());
            }
        }
    }
    DNSRecord record;
    record.ip_addresses = ips;
    record.expire_time = std::chrono::system_clock::now() + ttl_;
    record.is_valid = true;
    cache_[hostname] = std::move(record);
}

bool DNSCache::get(const std::string &hostname, std::vector<std::string> &ips) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = cache_.find(hostname);
    if (it == cache_.end()) {
        ++misses_;
        return false;
    }
    auto &record = it->second;
    const auto now = std::chrono::system_clock::now();
    if (now >= record.expire_time || !record.is_valid) {
        cache_.erase(it);
        ++misses_;
        return false;
    }

    ips = record.ip_addresses;
    ++hits_;

    // 如果记录即将过期（TTL的20%以内），异步刷新
    const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                                   record.expire_time - now)
                                   .count();
    if (remaining < static_cast<int64_t>(ttl_.count() * 0.2)) {
        record.is_valid = false;// 标记为需要刷新
    }
    return true;
}

void DNSCache::remove(const std::string &hostname) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.erase(hostname);
}

void DNSCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    hits_ = 0;
    misses_ = 0;
}

void DNSCache::forEach(const ForEachFn &fn) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &[hostname, record]: cache_) {
        fn(hostname, record);
    }
}

size_t DNSCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

size_t DNSCache::capacity() const {
    return max_size_;
}

double DNSCache::hit_rate() const {
    const auto total = hits_.load() + misses_.load();
    if (total == 0) {
        return 0.0;
    }
    return static_cast<double>(hits_.load()) / total;
}

void DNSCache::cleanup() {
    auto now = std::chrono::system_clock::now();
    // 使用删除-擦除习语
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (now >= it->second.expire_time || !it->second.is_valid) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
    // 如果缓存大小超过最大限制的90%，主动清理最早过期的记录
    if (cache_.size() > max_size_ * 0.9) {
        std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> expire_times;
        expire_times.reserve(cache_.size());
        for (const auto &[hostname, record]: cache_) {
            expire_times.emplace_back(hostname, record.expire_time);
        }
        // 按过期时间排序
        std::ranges::sort(expire_times,
                          [](const auto &a, const auto &b) {
                              return a.second < b.second;
                          });

        // 删除20%的最早过期记录
        const auto records_to_remove = static_cast<size_t>(cache_.size() * 0.2);
        for (size_t i = 0; i < records_to_remove && i < expire_times.size(); ++i) {
            cache_.erase(expire_times[i].first);
        }
    }
}