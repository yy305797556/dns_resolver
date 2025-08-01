#ifndef LEIGOD_DNSCOMMON_H
#define LEIGOD_DNSCOMMON_H

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace leigod {
    namespace dns {
        // DNS解析结果结构
        struct DNSResult {
            std::vector<std::string> ipv4;
            std::vector<std::string> ipv6;
            std::string error;
        };

        // 回调函数类型
        using DNSCallback = std::function<void(const DNSResult &)>;

        // 缓存条目
        struct CacheEntry {
            std::vector<std::string> ips;
            std::chrono::system_clock::time_point expiry;
            bool isValid() const {
                return std::chrono::system_clock::now() < expiry;
            }
        };
    }// namespace dns
}// namespace leigod

#endif//LEIGOD_DNSCOMMON_H
