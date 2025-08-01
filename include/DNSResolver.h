#pragma once

#include "DNSCache.h"
#include "DNSConfig.h"
#include "DNSMetrics.h"
#include <ares.h>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

class DNSResolver : public std::enable_shared_from_this<DNSResolver> {
public:
    struct ResolveResult {
        std::string hostname;
        std::vector<std::string> ip_addresses;
        int status;
        std::chrono::milliseconds resolution_time;
    };

    DNSResolver();
    virtual ~DNSResolver();

    // 初始化
    bool init(const std::vector<std::string> &dns_servers, std::chrono::seconds cache_ttl = std::chrono::seconds(300));

    // 配置相关
    bool loadConfig(const std::string &config_file);
    bool loadConfig(const DNSResolverConfig &config);
    bool reloadConfig();

    // DNS解析
    std::future<ResolveResult> resolve(const std::string &hostname);
    std::vector<std::future<ResolveResult>> resolve_batch(const std::vector<std::string> &hostnames);
    std::future<ResolveResult> refresh(const std::string &hostname);

    // 缓存操作
    void clear_cache();
    [[nodiscard]] bool save_cache(const std::string &filename) const;
    bool load_cache(const std::string &filename);
    [[nodiscard]] std::shared_ptr<DNSCache> getCache() const;
    [[nodiscard]] std::shared_ptr<DNSMetrics> getMetrics() const;

    // 获取统计信息
    DNSMetrics::Stats getStats() const;

private:
    struct QueryContext {
        std::string hostname;
        std::promise<ResolveResult> promise;
        std::chrono::steady_clock::time_point start_time;
        std::vector<char> buffer;
        std::shared_ptr<DNSResolver> resolver;
    };

    static void socket_callback(void *data, ares_socket_t socket_fd, int readable, int writable);
    static void addrinfo_callback(void *arg, int status, int timeouts, struct ares_addrinfo *result);
    void process_result(QueryContext *context, int status, const struct ares_addrinfo *result);
    void notifyAddressChange(const std::string &hostname, const std::vector<std::string> &old_addresses,
                             const std::vector<std::string> &new_addresses, const std::string &source);
    void wait_for_completion();

    ares_channel channel_{};
    bool initialized_{};
    std::shared_ptr<DNSCache> cache_{};
    std::shared_ptr<DNSMetrics> metrics_{};
    std::shared_ptr<DNSResolverConfig> config_{};
    std::vector<std::string> dns_server_list_{};
    std::unordered_map<ares_socket_t, std::string> socket_server_map_;
};