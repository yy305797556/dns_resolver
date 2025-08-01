#pragma once

#include "DNSConfig.h"
#include <stdexcept>
#include <string>

class ConfigValidationError : public std::runtime_error {
public:
    explicit ConfigValidationError(const std::string &msg)
        : std::runtime_error(msg) {}
};

class DNSConfigValidator {
public:
    static void validate(const DNSResolverConfig &config);

private:
    static void validateServers(const std::vector<DNSServerConfig> &servers);
    static void validateCache(const CacheConfig &cache);
    static void validateRetry(const RetryConfig &retry);
    static void validateMetrics(const MetricsConfig &metrics);
    static bool isValidIpAddress(const std::string &ip);
    static bool isValidPath(const std::string &path);
};