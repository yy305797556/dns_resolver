#include "DNSConfigValidator.h"
#include <filesystem>
#include <set>

// #include <arpa/inet.h>
// #include <sys/stat.h>
#include <fstream>
#include <ws2tcpip.h>

void DNSConfigValidator::validate(const DNSResolverConfig &config) {
    validateServers(config.servers());
    validateCache(config.cache());
    validateRetry(config.retry());
    validateMetrics(config.metrics());

    if (config.query_timeout_ms() < 100 ||
        config.query_timeout_ms() > 30000) {
        throw ConfigValidationError("Query timeout must be between 100ms and 30000ms");
    }

    if (config.max_concurrent_queries() < 1 ||
        config.max_concurrent_queries() > 10000) {
        throw ConfigValidationError("Max concurrent queries must be between 1 and 10000");
    }

    // 检查服务器优先级和权重分布
    double total_weight = 0;
    for (const auto &server: config.servers()) {
        if (server.enabled) {
            total_weight += server.weight;
        }
    }

    if (total_weight <= 0) {
        throw ConfigValidationError("Total weight of enabled servers must be positive");
    }

    // 检查是否有重复的服务器地址
    std::set<std::string> server_addresses;
    for (const auto &server: config.servers()) {
        if (!server_addresses.insert(server.address).second) {
            throw ConfigValidationError("Duplicate server address: " + server.address);
        }
    }
}

void DNSConfigValidator::validateServers(const std::vector<DNSServerConfig> &servers) {

    if (servers.empty()) {
        throw ConfigValidationError("At least one DNS server must be configured");
    }

    bool hasEnabledServer = false;
    for (const auto &server: servers) {
        if (!isValidIpAddress(server.address)) {
            throw ConfigValidationError("Invalid server IP address: " + server.address);
        }

        if (server.port == 0 || server.port > 65535) {
            throw ConfigValidationError("Invalid server port for " + server.address + ": " + std::to_string(server.port));
        }

        if (server.timeout_ms < 100 || server.timeout_ms > 10000) {
            throw ConfigValidationError("Invalid timeout for server " + server.address + ": " + std::to_string(server.timeout_ms) + "ms");
        }

        if (server.weight < 1 || server.weight > 100) {
            throw ConfigValidationError("Invalid weight for server " + server.address + ": " + std::to_string(server.weight));
        }

        if (server.weight < 1 || server.weight > 100) {
            throw ConfigValidationError("Invalid weight for server " + server.address + ": " + std::to_string(server.weight));
        }

        if (server.enabled) {
            hasEnabledServer = true;
        }
    }

    if (!hasEnabledServer) {
        throw ConfigValidationError("At least one server must be enabled");
    }
}

void DNSConfigValidator::validateCache(const CacheConfig &cache) {
    if (cache.enabled) {
        if (cache.ttl.count() < 1 || cache.ttl.count() > 86400) {
            throw ConfigValidationError("Cache TTL must be between 1 and 86400 seconds");
        }

        if (cache.max_size < 100 || cache.max_size > 1000000) {
            throw ConfigValidationError("Cache max size must be between 100 and 1000000 entries");
        }

        if (cache.persistent && !cache.cache_file.empty()) {
            if (!isValidPath(cache.cache_file)) {
                throw ConfigValidationError("Invalid cache file path: " + cache.cache_file);
            }
            // 检查目录是否可写
            std::filesystem::path cachePath(cache.cache_file);
            auto parentPath = cachePath.parent_path();
            if (!std::filesystem::exists(parentPath)) {
                try {
                    std::filesystem::create_directories(parentPath);
                } catch (const std::filesystem::filesystem_error &e) {
                    throw ConfigValidationError("Cannot create cache directory: " + std::string(e.what()));
                }
            }
            if (!std::filesystem::is_directory(parentPath)) {
                throw ConfigValidationError("Cache parent path is not a directory: " + parentPath.string());
            }
            // 检查文件权限
            try {
                std::ofstream test(cache.cache_file, std::ios::app | std::ios::binary);
                if (!test.is_open()) {
                    throw ConfigValidationError("Cannot write to cache file: " + cache.cache_file);
                }
            } catch (const std::exception &e) {
                throw ConfigValidationError("Cache file access error: " + std::string(e.what()));
            }
        }
    }
}

void DNSConfigValidator::validateRetry(const RetryConfig &retry) {
    if (retry.max_attempts < 1 || retry.max_attempts > 10) {
        throw ConfigValidationError("Max retry attempts must be between 1 and 10");
    }

    if (retry.base_delay_ms < 50 || retry.base_delay_ms > 1000) {
        throw ConfigValidationError("Base retry delay must be between 50ms and 1000ms");
    }

    if (retry.max_delay_ms < retry.base_delay_ms ||
        retry.max_delay_ms > 10000) {
        throw ConfigValidationError("Max retry delay must be between base delay and 10000ms");
    }

    // 验证指数退避策略的合理性
    uint32_t max_possible_delay = retry.base_delay_ms;
    for (uint32_t i = 1; i < retry.max_attempts; ++i) {
        max_possible_delay *= 2;
        if (max_possible_delay > retry.max_delay_ms) {
            max_possible_delay = retry.max_delay_ms;
            break;
        }
    }

    if (max_possible_delay > retry.max_delay_ms) {
        throw ConfigValidationError("Retry delay progression exceeds max delay");
    }
}

void DNSConfigValidator::validateMetrics(const MetricsConfig &metrics) {
    if (metrics.enabled) {
        if (metrics.report_interval_sec < 1 ||
            metrics.report_interval_sec > 3600) {
            throw ConfigValidationError("Metrics report interval must be between 1 and 3600 seconds");
        }

        if (!metrics.metrics_file.empty() && !isValidPath(metrics.metrics_file)) {
            throw ConfigValidationError("Invalid metrics file path: " + metrics.metrics_file);
        }

        // 验证Prometheus地址格式
        const size_t pos = metrics.prometheus_address.find(':');
        if (pos == std::string::npos) {
            throw ConfigValidationError("Invalid Prometheus address format: " + metrics.prometheus_address);
        }

        std::string host = metrics.prometheus_address.substr(0, pos);
        std::string port_str = metrics.prometheus_address.substr(pos + 1);

        try {
            int port = std::stoi(port_str);
            if (port < 1 || port > 65535) {
                throw ConfigValidationError("Invalid Prometheus port: " + port_str);
            }
        } catch (const std::exception &) {
            throw ConfigValidationError("Invalid Prometheus port: " + port_str);
        }
    }
}

bool DNSConfigValidator::isValidIpAddress(const std::string &ip) {
    struct sockaddr_in sa4{};
    struct sockaddr_in6 sa6{};
    // 检查IPv4地址
    if (inet_pton(AF_INET, ip.c_str(), &(sa4.sin_addr)) == 1) {
        return true;
    }

    // 检查IPv6地址
    if (inet_pton(AF_INET6, ip.c_str(), &(sa6.sin6_addr)) == 1) {
        return true;
    }

    return false;
}

bool DNSConfigValidator::isValidPath(const std::string &path) {
    if (path.empty() || path.length() > 4096) {
        return false;
    }

    // 检查路径是否包含非法字符
    const std::string invalid_chars = "<>:\"|?*";
    if (path.find_first_of(invalid_chars) != std::string::npos) {
        return false;
    }

    // 检查路径是否为绝对路径
    if (path[0] != '/') {
        return false;
    }

    try {
        const std::filesystem::path fsPath(path);
        const auto parent = fsPath.parent_path();
        if (parent.empty()) {
            return false;
        }
        return std::filesystem::exists(parent) && std::filesystem::is_directory(parent);
    } catch (const std::exception &) {
        return false;
    }
}