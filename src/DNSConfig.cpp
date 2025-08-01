#include "DNSConfig.h"

#include "DNSConfigValidator.h"

#include <fstream>
#include <iostream>

DNSServerConfigBuilder &DNSServerConfigBuilder::setAddress(const std::string &address) {
    config_.address = address;
    return *this;
}

DNSServerConfigBuilder &DNSServerConfigBuilder::setPort(uint16_t port) {
    config_.port = port;
    return *this;
}

DNSServerConfigBuilder &DNSServerConfigBuilder::setWeight(uint32_t weight) {
    config_.weight = weight;
    return *this;
}

DNSServerConfigBuilder &DNSServerConfigBuilder::setTimeout(uint32_t timeout_ms) {
    config_.timeout_ms = timeout_ms;
    return *this;
}

DNSServerConfigBuilder &DNSServerConfigBuilder::setEnabled(bool enabled) {
    config_.enabled = enabled;
    return *this;
}

DNSServerConfig DNSServerConfigBuilder::build() const {
    return config_;
}

// DNSResolverConfig implementation
DNSResolverConfig &DNSResolverConfig::getInstance() {
    static DNSResolverConfig instance;
    return instance;
}

DNSResolverConfig::DNSResolverConfig() {

    // 默认DNS服务器配置
    servers_.push_back({
            "114.114.114.114",//
            53,               // 端口
            1,                // 权重
            2000,             // 超时
            true              // 启用
    });

    // 默认缓存配置
    cache_.enabled = true;
    cache_.ttl = std::chrono::seconds(300);
    cache_.max_size = 10000;
    cache_.persistent = false;
    cache_.cache_file = "";

    // 默认重试配置
    retry_.max_attempts = 3;
    retry_.base_delay_ms = 100;
    retry_.max_delay_ms = 1000;

    // 默认监控配置
    metrics_.enabled = true;
    metrics_.metrics_file = "";
    metrics_.report_interval_sec = 60;
    metrics_.prometheus_address = "0.0.0.0:9091";
}

bool DNSResolverConfig::loadFromFile(const std::string &filename) {
    try {
        YAML::Node config = YAML::LoadFile(filename);

        // 加载DNS服务器配置
        if (config["servers"]) {
            servers_.clear();
            for (const auto &server: config["servers"]) {
                DNSServerConfig srv;
                srv.address = server["address"].as<std::string>();
                srv.port = server["port"].as<uint16_t>(53);
                srv.weight = server["weight"].as<uint32_t>(1);
                srv.timeout_ms = server["timeout_ms"].as<uint32_t>(2000);
                srv.enabled = server["enabled"].as<bool>(true);
                servers_.push_back(srv);
            }
        }

        // 加载缓存配置
        if (config["cache"]) {
            auto cache = config["cache"];
            cache_.enabled = cache["enabled"].as<bool>(true);
            cache_.ttl = std::chrono::seconds(cache["ttl_seconds"].as<uint32_t>(300));
            cache_.max_size = cache["max_size"].as<size_t>(10000);
            cache_.persistent = cache["persistent"].as<bool>(false);
            cache_.cache_file = cache["cache_file"].as<std::string>("");
        }

        // 加载重试配置
        if (config["retry"]) {
            auto retry = config["retry"];
            retry_.max_attempts = retry["max_attempts"].as<uint32_t>(3);
            retry_.base_delay_ms = retry["base_delay_ms"].as<uint32_t>(100);
            retry_.max_delay_ms = retry["max_delay_ms"].as<uint32_t>(1000);
        }

        // 加载监控配置
        if (config["metrics"]) {
            auto metrics = config["metrics"];
            metrics_.enabled = metrics["enabled"].as<bool>(true);
            metrics_.metrics_file = metrics["file"].as<std::string>("");
            metrics_.report_interval_sec = metrics["report_interval_sec"].as<uint32_t>(60);
            metrics_.prometheus_address = metrics["prometheus_address"].as<std::string>("0.0.0.0:9091");
        }

        // 加载全局配置
        if (config["global"]) {
            auto global = config["global"];
            query_timeout_ms_ = global["query_timeout_ms"].as<uint32_t>(5000);
            max_concurrent_queries_ = global["max_concurrent_queries"].as<uint32_t>(100);
            ipv6_enabled_ = global["ipv6_enabled"].as<bool>(true);
        }

        // 验证配置
        DNSConfigValidator::validate(*this);

        return true;
    } catch (const YAML::Exception &e) {
        std::cerr << "YAML parsing error: " << e.what() << std::endl;
        return false;
    } catch (const ConfigValidationError &e) {
        std::cerr << "Configuration validation error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception &e) {
        std::cerr << "Error loading configuration: " << e.what() << std::endl;
        return false;
    }
}

bool DNSResolverConfig::saveToFile(const std::string &filename) const {
    try {
        YAML::Node config;

        // 保存DNS服务器配置
        YAML::Node servers;
        for (const auto &srv: servers_) {
            YAML::Node server;
            server["address"] = srv.address;
            server["port"] = srv.port;
            server["weight"] = srv.weight;
            server["timeout_ms"] = srv.timeout_ms;
            server["enabled"] = srv.enabled;
            servers.push_back(server);
        }
        config["servers"] = servers;

        // 保存缓存配置
        YAML::Node cache;
        cache["enabled"] = cache_.enabled;
        cache["ttl_seconds"] = cache_.ttl.count();
        cache["max_size"] = cache_.max_size;
        cache["persistent"] = cache_.persistent;
        cache["cache_file"] = cache_.cache_file;
        config["cache"] = cache;

        // 保存重试配置
        YAML::Node retry;
        retry["max_attempts"] = retry_.max_attempts;
        retry["base_delay_ms"] = retry_.base_delay_ms;
        retry["max_delay_ms"] = retry_.max_delay_ms;
        config["retry"] = retry;

        // 保存监控配置
        YAML::Node metrics;
        metrics["enabled"] = metrics_.enabled;
        metrics["file"] = metrics_.metrics_file;
        metrics["report_interval_sec"] = metrics_.report_interval_sec;
        metrics["prometheus_address"] = metrics_.prometheus_address;
        config["metrics"] = metrics;

        // 保存全局配置
        YAML::Node global;
        global["query_timeout_ms"] = query_timeout_ms_;
        global["max_concurrent_queries"] = max_concurrent_queries_;
        global["ipv6_enabled"] = ipv6_enabled_;
        config["global"] = global;

        // 添加元数据
        config["metadata"]["version"] = "1.0";

        std::ofstream fout(filename);
        fout << config;
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Error saving configuration: " << e.what() << std::endl;
        return false;
    }
}

void DNSResolverConfig::addServer(const DNSServerConfig &server) {
    // 检查是否存在重复地址
    for (const auto &existing: servers_) {
        if (existing.address == server.address) {
            throw ConfigValidationError(
                    "Server with address " + server.address + " already exists");
        }
    }
    servers_.push_back(server);
}

void DNSResolverConfig::removeServer(const std::string &address) {
    servers_.erase(
            std::remove_if(
                    servers_.begin(),
                    servers_.end(),
                    [&address](const DNSServerConfig &server) {
                        return server.address == address;
                    }),
            servers_.end());

    // 确保至少保留一个启用的服务器
    bool has_enabled = false;
    for (const auto &server: servers_) {
        if (server.enabled) {
            has_enabled = true;
            break;
        }
    }

    if (!has_enabled && !servers_.empty()) {
        servers_[0].enabled = true;
    }
}

void DNSResolverConfig::updateServer(const DNSServerConfig &server) {
    for (auto &existing: servers_) {
        if (existing.address == server.address) {
            existing = server;
            return;
        }
    }
    // 如果未找到服务器，则添加
    addServer(server);
}

void DNSResolverConfig::setServers(const std::vector<DNSServerConfig> &servers) {
    // 验证服务器配置
    std::set<std::string> addresses;
    bool has_enabled = false;

    for (const auto &server: servers) {
        if (!addresses.insert(server.address).second) {
            throw ConfigValidationError(
                    "Duplicate server address: " + server.address);
        }
        if (server.enabled) {
            has_enabled = true;
        }
    }

    if (!servers.empty() && !has_enabled) {
        throw ConfigValidationError(
                "At least one server must be enabled");
    }

    servers_ = servers;
}

void DNSResolverConfig::setCacheConfig(const CacheConfig &cache) {
    if (cache.ttl.count() < 1 || cache.ttl.count() > 86400) {
        throw ConfigValidationError(
                "Cache TTL must be between 1 and 86400 seconds");
    }

    if (cache.max_size < 100 || cache.max_size > 1000000) {
        throw ConfigValidationError(
                "Cache max size must be between 100 and 1000000 entries");
    }

    cache_ = cache;
}

void DNSResolverConfig::setRetryConfig(const RetryConfig &retry) {
    if (retry.max_attempts < 1 || retry.max_attempts > 10) {
        throw ConfigValidationError(
                "Max retry attempts must be between 1 and 10");
    }

    if (retry.base_delay_ms < 50 || retry.base_delay_ms > 1000) {
        throw ConfigValidationError(
                "Base retry delay must be between 50ms and 1000ms");
    }

    if (retry.max_delay_ms < retry.base_delay_ms ||
        retry.max_delay_ms > 10000) {
        throw ConfigValidationError(
                "Max retry delay must be between base delay and 10000ms");
    }

    retry_ = retry;
}

void DNSResolverConfig::setMetricsConfig(const MetricsConfig &metrics) {
    if (metrics.enabled && metrics.report_interval_sec < 1) {
        throw ConfigValidationError(
                "Metrics report interval must be at least 1 second");
    }

    metrics_ = metrics;
}

void DNSResolverConfig::setQueryTimeout(uint32_t timeout_ms) {
    if (timeout_ms < 100 || timeout_ms > 30000) {
        throw ConfigValidationError(
                "Query timeout must be between 100ms and 30000ms");
    }
    query_timeout_ms_ = timeout_ms;
}

void DNSResolverConfig::setMaxConcurrentQueries(uint32_t max_queries) {
    if (max_queries < 1 || max_queries > 10000) {
        throw ConfigValidationError(
                "Max concurrent queries must be between 1 and 10000");
    }
    max_concurrent_queries_ = max_queries;
}

void DNSResolverConfig::setIPv6Enabled(bool enabled) {
    ipv6_enabled_ = enabled;
}

void DNSResolverConfig::update(const DNSResolverConfig &other) {
    servers_ = other.servers_;
    cache_ = other.cache_;
    retry_ = other.retry_;
    metrics_ = other.metrics_;
    query_timeout_ms_ = other.query_timeout_ms_;
    max_concurrent_queries_ = other.max_concurrent_queries_;
    ipv6_enabled_ = other.ipv6_enabled_;
}

DNSResolverConfig DNSResolverConfig::clone() const {
    DNSResolverConfig copy;
    copy.update(*this);
    return copy;
}

DNSResolverConfigBuilder::DNSResolverConfigBuilder()
    : query_timeout_ms_(5000), max_concurrent_queries_(100), ipv6_enabled_(true) {
    // 设置默认缓存配置
    cache_.enabled = true;
    cache_.ttl = std::chrono::seconds(300);
    cache_.max_size = 10000;
    cache_.persistent = false;
    cache_.cache_file = "";

    // 设置默认重试配置
    retry_.max_attempts = 3;
    retry_.base_delay_ms = 100;
    retry_.max_delay_ms = 1000;

    // 设置默认监控配置
    metrics_.enabled = true;
    metrics_.metrics_file = "";
    metrics_.report_interval_sec = 60;
    metrics_.prometheus_address = "0.0.0.0:9091";
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::addServer(const DNSServerConfig &server) {
    servers_.push_back(server);
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::clearServers() {
    servers_.clear();
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setCacheEnabled(
        bool enabled) {
    cache_.enabled = enabled;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setCacheTTL(
        std::chrono::seconds ttl) {
    cache_.ttl = ttl;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setCacheMaxSize(
        size_t max_size) {
    cache_.max_size = max_size;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setCachePersistent(
        bool persistent) {
    cache_.persistent = persistent;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setCacheFile(
        const std::string &file) {
    cache_.cache_file = file;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setRetryAttempts(
        uint32_t attempts) {
    retry_.max_attempts = attempts;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setRetryBaseDelay(
        uint32_t delay_ms) {
    retry_.base_delay_ms = delay_ms;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setRetryMaxDelay(
        uint32_t delay_ms) {
    retry_.max_delay_ms = delay_ms;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setMetricsEnabled(
        bool enabled) {
    metrics_.enabled = enabled;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setMetricsFile(
        const std::string &file) {
    metrics_.metrics_file = file;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setMetricsInterval(
        uint32_t interval_sec) {
    metrics_.report_interval_sec = interval_sec;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setPrometheusAddress(
        const std::string &address) {
    metrics_.prometheus_address = address;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setQueryTimeout(
        uint32_t timeout_ms) {
    query_timeout_ms_ = timeout_ms;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setMaxConcurrentQueries(
        uint32_t max_queries) {
    max_concurrent_queries_ = max_queries;
    return *this;
}

DNSResolverConfigBuilder &DNSResolverConfigBuilder::setIPv6Enabled(
        bool enabled) {
    ipv6_enabled_ = enabled;
    return *this;
}

DNSResolverConfig DNSResolverConfigBuilder::build() const {
    DNSResolverConfig config;

    // 验证并设置所有配置
    try {
        config.setServers(servers_);
        config.setCacheConfig(cache_);
        config.setRetryConfig(retry_);
        config.setMetricsConfig(metrics_);
        config.setQueryTimeout(query_timeout_ms_);
        config.setMaxConcurrentQueries(max_concurrent_queries_);
        config.setIPv6Enabled(ipv6_enabled_);
    } catch (const ConfigValidationError &e) {
        throw ConfigValidationError(
                std::string("Configuration validation failed during build: ") +
                e.what());
    }

    return config;
}