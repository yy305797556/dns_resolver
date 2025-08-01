#include "DNSMetrics.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numeric>
#include <sstream>

constexpr size_t MAX_SAMPLES = 1000;

DNSMetrics::DNSMetrics()
    : registry_(std::make_shared<prometheus::Registry>()),
      total_queries_(prometheus::BuildCounter()
                             .Name("dns_total_queries")
                             .Help("Total number of DNS queries")
                             .Register(*registry_)
                             .Add({})),
      successful_queries_(prometheus::BuildCounter()
                                  .Name("dns_successful_queries")
                                  .Help("Number of successful DNS queries")
                                  .Register(*registry_)
                                  .Add({})),
      failed_queries_(prometheus::BuildCounter()
                              .Name("dns_failed_queries")
                              .Help("Number of failed DNS queries")
                              .Register(*registry_)
                              .Add({})),
      cache_hits_(prometheus::BuildCounter()
                          .Name("dns_cache_hits")
                          .Help("Number of cache hits")
                          .Register(*registry_)
                          .Add({})),
      cache_misses_(prometheus::BuildCounter()
                            .Name("dns_cache_misses")
                            .Help("Number of cache misses")
                            .Register(*registry_)
                            .Add({})),
      query_duration_(prometheus::BuildHistogram()
                              .Name("dns_query_duration_seconds")
                              .Help("DNS query duration in seconds")
                              .Register(*registry_)
                              .Add({}, std::vector<double>{0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1, 5})),
      cache_hit_rate_(prometheus::BuildGauge()
                              .Name("dns_cache_hit_rate")
                              .Help("Cache hit rate")
                              .Register(*registry_)
                              .Add({})),
      total_retries_(prometheus::BuildCounter()
                             .Name("dns_total_retries_")
                             .Help("Total number of DNS retries")
                             .Register(*registry_)
                             .Add({})) {
}

void DNSMetrics::startPrometheusExporter(const std::string &address) {
    try {
        exposer_ = std::make_unique<prometheus::Exposer>(address);
        exposer_->RegisterCollectable(registry_);
    } catch (const std::exception &e) {
        std::cerr << "Failed to start Prometheus exporter: "
                  << e.what() << std::endl;
    }
}

void DNSMetrics::recordQuery(const std::string &hostname, std::chrono::milliseconds duration, bool success) {
    total_queries_.Increment();
    if (success) {
        successful_queries_.Increment();
    } else {
        failed_queries_.Increment();
    }
    query_duration_.Observe(duration.count());
    // 检查延迟是否超过阈值
    if (duration > latency_threshold_) {
        std::string alert = "High latency detected for " + hostname + ": " + std::to_string(duration.count()) + "ms";
        for (const auto &callback: alert_callbacks_) {
            callback(alert);
        }
    }
    // 计算错误率并检查阈值
    auto total = successful_queries_.Value() + failed_queries_.Value();
    if (total > 0) {
        double error_rate = failed_queries_.Value() / total;
        if (error_rate > error_rate_threshold_) {
            std::string alert = "High error rate detected: " + std::to_string(error_rate * 100) + "%";
            for (const auto &callback: alert_callbacks_) {
                callback(alert);
            }
        }
    }
}

void DNSMetrics::recordCacheHit(const std::string &hostname) {
    cache_hits_.Increment();
    updateCacheHitRate();
}

void DNSMetrics::recordCacheMiss(const std::string &hostname) {
    cache_misses_.Increment();
    updateCacheHitRate();
}

void DNSMetrics::recordServerLatency(const std::string &server, std::chrono::milliseconds latency) {
    std::lock_guard<std::mutex> lock(latency_mutex_);
    server_latencies_[server].push_back(latency.count());
    // 保持最近1000个样本
    if (server_latencies_[server].size() > MAX_SAMPLES) {
        server_latencies_[server].erase(
                server_latencies_[server].begin(),
                server_latencies_[server].begin() + (server_latencies_[server].size() - MAX_SAMPLES));
    }

    // 检查延迟阈值
    if (latency > latency_threshold_) {
        std::string alert = "High server latency detected for " + server + ": " + std::to_string(latency.count()) + "ms";
        for (const auto &callback: alert_callbacks_) {
            callback(alert);
        }
    }
}

void DNSMetrics::recordError(const std::string &type, const std::string &detail) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    error_counts_[type]++;

    // 记录详细错误信息到Prometheus
    prometheus::Counter &error_counter = prometheus::BuildCounter()
                                                 .Name("dns_error_" + type)
                                                 .Help("Number of " + type + " errors")
                                                 .Register(*registry_)
                                                 .Add({});
    error_counter.Increment();
}

void DNSMetrics::recordRetry(const std::string &hostname, uint32_t attempt) {
    total_retries_.Increment();
    std::lock_guard<std::mutex> lock(retry_mutex_);
    retry_attempts_[hostname].push_back(attempt);

    // 保持最近100次重试记录
    constexpr size_t MAX_RETRY_HISTORY = 100;
    if (retry_attempts_[hostname].size() > MAX_RETRY_HISTORY) {
        retry_attempts_[hostname].erase(retry_attempts_[hostname].begin(),
                                        retry_attempts_[hostname].begin() + (retry_attempts_[hostname].size() - MAX_RETRY_HISTORY));
    }
}

DNSMetrics::Stats DNSMetrics::getStats() const {
    Stats stats;
    stats.total_queries = static_cast<int64_t>(total_queries_.Value());
    stats.successful_queries = static_cast<int64_t>(successful_queries_.Value());
    stats.failed_queries = static_cast<int64_t>(failed_queries_.Value());
    stats.cache_hits = static_cast<int64_t>(cache_hits_.Value());
    stats.cache_misses = static_cast<int64_t>(cache_misses_.Value());

    const double total = stats.cache_hits + stats.cache_misses;
    stats.cache_hit_rate = total > 0 ? static_cast<int64_t>(stats.cache_hits / total) : 0;

    // 复制错误计数
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        stats.error_counts = error_counts_;
    }
    // 计算服务器延迟
    {
        std::lock_guard<std::mutex> lock(latency_mutex_);
        for (const auto &[server, latencies]: server_latencies_) {
            if (!latencies.empty()) {
                double avg_latency = std::accumulate(
                                             latencies.begin(), latencies.end(), 0.0) /
                                     latencies.size();
                stats.server_latencies[server] = avg_latency;
            }
        }
    }

    // 统计重试信息
    {
        std::lock_guard<std::mutex> lock(retry_mutex_);
        stats.total_retries = static_cast<int64_t>(total_retries_.Value());
        stats.retry_attempts = retry_attempts_;
    }
    return stats;
}

void DNSMetrics::resetStats() {
    std::lock_guard<std::mutex> error_lock(error_mutex_);
    std::lock_guard<std::mutex> latency_lock(latency_mutex_);
    error_counts_.clear();
    server_latencies_.clear();
}

void DNSMetrics::updateCacheHitRate() {
    double total = cache_hits_.Value() + cache_misses_.Value();
    if (total > 0) {
        cache_hit_rate_.Set(cache_hits_.Value() / total);
    }
}

void DNSMetrics::setAlertThresholds(double error_rate_threshold, std::chrono::milliseconds latency_threshold) {
    if (error_rate_threshold < 0.0 || error_rate_threshold > 1.0) {
        throw std::invalid_argument("Error rate threshold must be between 0 and 1");
    }
    error_rate_threshold_ = error_rate_threshold;

    if (latency_threshold.count() <= 0) {
        throw std::invalid_argument("Latency threshold must be positive");
    }
    latency_threshold_ = latency_threshold;
}

void DNSMetrics::registerAlertCallback(AlertCallback callback) {
    std::lock_guard<std::mutex> lock(alert_mutex_);
    alert_callbacks_.push_back(std::move(callback));
}

void DNSMetrics::clearAlertCallbacks() {
    std::lock_guard<std::mutex> lock(alert_mutex_);
    alert_callbacks_.clear();
}

void DNSMetrics::exportToFile(const std::string &filename) const {
    try {
        std::ofstream file(filename);
        if (!file) {
            throw std::runtime_error("Unable to open metrics file: " + filename);
        }

        auto stats = getStats();
        nlohmann::json j;

        // 基本统计信息
        j["timestamp"] = std::chrono::system_clock::now()
                                 .time_since_epoch()
                                 .count();
        j["total_queries"] = stats.total_queries;
        j["successful_queries"] = stats.successful_queries;
        j["failed_queries"] = stats.failed_queries;
        j["cache_hits"] = stats.cache_hits;
        j["cache_misses"] = stats.cache_misses;
        j["cache_hit_rate"] = stats.cache_hit_rate;
        j["avg_query_time_ms"] = stats.avg_query_time_ms;
        j["total_retries"] = stats.total_retries;

        // 服务器延迟
        j["server_latencies"] = stats.server_latencies;

        // 错误统计
        j["error_counts"] = stats.error_counts;

        // 重试统计
        nlohmann::json retry_stats;
        for (const auto &[hostname, attempts]: stats.retry_attempts) {
            retry_stats[hostname] = attempts;
        }
        j["retry_attempts"] = retry_stats;

        file << std::setw(4) << j << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Failed to export metrics: " << e.what() << std::endl;
    }
}