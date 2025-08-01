#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#include <string>

class DNSMetrics {
public:
    using AlertCallback = std::function<void(const std::string &)>;

    DNSMetrics();
    void recordQuery(const std::string &hostname, std::chrono::milliseconds duration, bool success);
    void recordCacheHit(const std::string &hostname);
    void recordCacheMiss(const std::string &hostname);
    void recordServerLatency(const std::string &server, std::chrono::milliseconds latency);
    void recordError(const std::string &type, const std::string &detail);
    void recordRetry(const std::string &hostname, uint32_t attempt);
    void startPrometheusExporter(const std::string &address);

    struct Stats {
        uint64_t total_queries{};
        uint64_t successful_queries{};
        uint64_t failed_queries{};
        uint64_t cache_hits{};
        uint64_t cache_misses{};
        double cache_hit_rate{};
        double avg_query_time_ms{};
        std::map<std::string, uint64_t> error_counts{};
        std::map<std::string, double> server_latencies{};
        uint64_t total_retries{};
        std::map<std::string, std::vector<uint32_t>> retry_attempts{};
    };

    Stats getStats() const;
    void resetStats();


    void setAlertThresholds(double error_rate_threshold, std::chrono::milliseconds latency_threshold);
    void registerAlertCallback(AlertCallback callback);
    void clearAlertCallbacks();

    void exportToFile(const std::string &filename) const;

private:
    std::shared_ptr<prometheus::Registry> registry_{};
    prometheus::Counter &total_queries_;
    prometheus::Counter &successful_queries_;
    prometheus::Counter &failed_queries_;
    prometheus::Counter &cache_hits_;
    prometheus::Counter &cache_misses_;
    prometheus::Histogram &query_duration_;
    prometheus::Gauge &cache_hit_rate_;
    prometheus::Counter &total_retries_;

    std::unique_ptr<prometheus::Exposer> exposer_{};

    mutable std::mutex error_mutex_;
    std::map<std::string, uint64_t> error_counts_{};

    mutable std::mutex latency_mutex_;
    std::map<std::string, std::vector<double>> server_latencies_{};

    mutable std::mutex retry_mutex_;
    std::map<std::string, std::vector<uint32_t>> retry_attempts_{};

    // 告警相关
    double error_rate_threshold_{};
    std::chrono::milliseconds latency_threshold_{};
    mutable std::mutex alert_mutex_;
    std::vector<AlertCallback> alert_callbacks_{};

    void updateCacheHitRate();
};