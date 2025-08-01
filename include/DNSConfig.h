#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

struct DNSServerConfig {
    std::string address;
    uint16_t port;
    uint32_t weight;
    uint32_t timeout_ms;
    bool enabled;
};

struct CacheConfig {
    bool enabled;
    std::chrono::seconds ttl;
    size_t max_size;
    bool persistent;
    std::string cache_file;
};

struct RetryConfig {
    uint32_t max_attempts;
    uint32_t base_delay_ms;
    uint32_t max_delay_ms;
};

struct MetricsConfig {
    bool enabled;
    std::string metrics_file;
    uint32_t report_interval_sec;
    std::string prometheus_address;
};

class DNSServerConfigBuilder {
public:
    DNSServerConfigBuilder &setAddress(const std::string &address);

    DNSServerConfigBuilder &setPort(uint16_t port);

    DNSServerConfigBuilder &setWeight(uint32_t weight);

    DNSServerConfigBuilder &setTimeout(uint32_t timeout_ms);

    DNSServerConfigBuilder &setEnabled(bool enabled);

    [[nodiscard]] DNSServerConfig build() const;

private:
    DNSServerConfig config_{};
};


class DNSResolverConfig {
public:
    static DNSResolverConfig &getInstance();

    DNSResolverConfig();

    bool loadFromFile(const std::string &filename);
    [[nodiscard]] bool saveToFile(const std::string &filename) const;
    // 配置访问器
    [[nodiscard]] const std::vector<DNSServerConfig> &servers() const { return servers_; }
    [[nodiscard]] CacheConfig &cache() { return cache_; }
    [[nodiscard]] RetryConfig &retry() { return retry_; }
    [[nodiscard]] MetricsConfig &metrics() { return metrics_; }
    [[nodiscard]] const CacheConfig &cache() const { return cache_; }
    [[nodiscard]] const RetryConfig &retry() const { return retry_; }
    [[nodiscard]] const MetricsConfig &metrics() const { return metrics_; }
    [[nodiscard]] uint32_t query_timeout_ms() const { return query_timeout_ms_; }
    [[nodiscard]] uint32_t max_concurrent_queries() const { return max_concurrent_queries_; }
    [[nodiscard]] bool ipv6_enabled() const { return ipv6_enabled_; }

    // 配置修改器
    void addServer(const DNSServerConfig &server);
    void removeServer(const std::string &address);
    void updateServer(const DNSServerConfig &server);
    void setServers(const std::vector<DNSServerConfig> &servers);

    void setCacheConfig(const CacheConfig &cache);
    void setRetryConfig(const RetryConfig &retry);
    void setMetricsConfig(const MetricsConfig &metrics);

    void setQueryTimeout(uint32_t timeout_ms);
    void setMaxConcurrentQueries(uint32_t max_queries);
    void setIPv6Enabled(bool enabled);

    // 应用配置更新
    void update(const DNSResolverConfig &other);
    // 创建配置副本
    [[nodiscard]] DNSResolverConfig clone() const;

private:
    std::vector<DNSServerConfig> servers_{};
    CacheConfig cache_{};
    RetryConfig retry_{};
    MetricsConfig metrics_{};

    uint32_t query_timeout_ms_ = 5000;
    uint32_t max_concurrent_queries_ = 100;
    bool ipv6_enabled_ = true;
};

class DNSResolverConfigBuilder {
public:
    DNSResolverConfigBuilder();

    // 服务器配置
    DNSResolverConfigBuilder &addServer(const DNSServerConfig &server);
    DNSResolverConfigBuilder &clearServers();

    // 缓存配置
    DNSResolverConfigBuilder &setCacheEnabled(bool enabled);
    DNSResolverConfigBuilder &setCacheTTL(std::chrono::seconds ttl);
    DNSResolverConfigBuilder &setCacheMaxSize(size_t max_size);
    DNSResolverConfigBuilder &setCachePersistent(bool persistent);
    DNSResolverConfigBuilder &setCacheFile(const std::string &file);

    // 重试配置
    DNSResolverConfigBuilder &setRetryAttempts(uint32_t attempts);
    DNSResolverConfigBuilder &setRetryBaseDelay(uint32_t delay_ms);
    DNSResolverConfigBuilder &setRetryMaxDelay(uint32_t delay_ms);

    // 监控配置
    DNSResolverConfigBuilder &setMetricsEnabled(bool enabled);
    DNSResolverConfigBuilder &setMetricsFile(const std::string &file);
    DNSResolverConfigBuilder &setMetricsInterval(uint32_t interval_sec);
    DNSResolverConfigBuilder &setPrometheusAddress(const std::string &address);

    // 全局配置
    DNSResolverConfigBuilder &setQueryTimeout(uint32_t timeout_ms);
    DNSResolverConfigBuilder &setMaxConcurrentQueries(uint32_t max_queries);
    DNSResolverConfigBuilder &setIPv6Enabled(bool enabled);

    // 构建配置
    [[nodiscard]] DNSResolverConfig build() const;

private:
    std::vector<DNSServerConfig> servers_;
    CacheConfig cache_;
    RetryConfig retry_{};
    MetricsConfig metrics_;
    uint32_t query_timeout_ms_;
    uint32_t max_concurrent_queries_;
    bool ipv6_enabled_;
};