#include "DNSConfig.h"
#include "DNSResolver.h"
#include <algorithm>
#include <chrono>
#include <future>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

// 打印查询结果
void printResult(const std::string &hostname,
                 const std::vector<std::string> &addresses,
                 std::chrono::milliseconds duration) {
    std::cout << "Hostname: " << std::left << std::setw(30) << hostname;
    std::cout << " Status: ";

    if (addresses.empty()) {
        std::cout << "Failed to resolve";
    } else {
        std::cout << "Resolved to " << addresses.size() << " address(es):";
        for (const auto &addr: addresses) {
            std::cout << " " << addr;
        }
    }

    std::cout << " (took " << duration.count() << "ms)" << std::endl;
}

int main(int argc, char *argv[]) {
    try {
        // 创建DNS配置
        DNSResolverConfig config;

        // 添加公共DNS服务器
        config.addServer({
                .address = "8.8.8.8",// Google DNS
                .port = 53,
                .weight = 1,
                .timeout_ms = 1000,
                .enabled = true,
        });

        config.addServer({
                .address = "1.1.1.1",// Cloudflare DNS
                .port = 53,
                .weight = 1,
                .timeout_ms = 1000,
                .enabled = true,
        });
        // 配置缓存
        config.cache().ttl = std::chrono::seconds(300);// 5分钟缓存
        config.cache().max_size = 1000;                // 最多缓存1000条记录

        // 创建解析器实例
        std::shared_ptr<DNSResolver> resolver = std::make_shared<DNSResolver>();

        if (!resolver->loadConfig(config)) {
            std::cerr << "Failed to load DNS configuration" << std::endl;
            return -1;
        }

        // 准备要查询的域名列表
        std::vector<std::string> domains = {
                "github.com",
                "google.com",
                "microsoft.com",
                "amazon.com",
                "facebook.com",
                "apple.com",
                "netflix.com",
                "twitter.com",
                "linkedin.com",
                "youtube.com"};

        std::cout << "Starting batch DNS resolution for "
                  << domains.size() << " domains...\n"
                  << std::endl;

        // 记录开始时间
        auto start_time = std::chrono::steady_clock::now();

        // 启动异步查询
        std::vector<std::future<DNSResolver::ResolveResult>> futures = resolver->resolve_batch(domains);

        // 等待并处理结果
        for (size_t i = 0; i < domains.size(); ++i) {
            try {
                auto query_start = std::chrono::steady_clock::now();
                auto addresses = futures[i].get();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - query_start);

                printResult(domains[i], addresses.ip_addresses, duration);

            } catch (const std::exception &e) {
                std::cout << "Error resolving " << domains[i]
                          << ": " << e.what() << std::endl;
            }
        }

        // 计算总耗时
        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);

        std::cout << "\nBatch resolution completed in "
                  << total_duration.count() << "ms" << std::endl;

        auto stats = resolver->getStats();

        // 输出缓存和性能统计
        std::cout << "\nCache Statistics:" << std::endl;
        std::cout << "Cache Size: " << stats.cache_hits + stats.cache_misses << std::endl;
        std::cout << "Cache Hit Rate: "
                  << std::fixed << std::setprecision(2)
                  << stats.cache_hit_rate * 100
                  << "%" << std::endl;

        // 输出服务器性能统计
        std::cout << "\nServer Performance:" << std::endl;
        for (const auto &server: config.servers()) {
            std::cout << "Server " << server.address << ":\n"
                      << "  Success Rate: "
                      << (stats.successful_queries * 100.0 / stats.total_queries)
                      << "%\n"
                      << "  Average Response Time: "
                      << stats.server_latencies[server.address]
                      << "ms\n";
        }

    } catch (const std::exception &e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}