#include "DNSConfig.h"
#include "DNSResolver.h"
#include <iostream>

int main(int agrc, char *agrv[]) {
    try {
        // 使用构建器模式创建配置
        auto config = DNSResolverConfigBuilder()
                              // 添加DNS服务器
                              .addServer(
                                      DNSServerConfigBuilder()
                                              .setAddress("8.8.8.8")
                                              .setPort(53)
                                              .setWeight(2)
                                              .setTimeout(2000)
                                              .setEnabled(true)
                                              .build())
                              .addServer(
                                      DNSServerConfigBuilder()
                                              .setAddress("114.114.114.114")
                                              .setPort(53)
                                              .setWeight(1)
                                              .setTimeout(2000)
                                              .setEnabled(true)
                                              .build())
                              // 配置缓存
                              .setCacheEnabled(true)
                              .setCacheTTL(std::chrono::seconds(300))
                              .setCacheMaxSize(10000)
                              .setCachePersistent(true)
                              .setCacheFile("/var/cache/dns_resolver/cache.dat")
                              // 配置重试策略
                              .setRetryAttempts(3)
                              .setRetryBaseDelay(100)
                              .setRetryMaxDelay(1000)
                              // 配置监控
                              .setMetricsEnabled(true)
                              .setMetricsFile("/var/log/dns_resolver/metrics.log")
                              .setMetricsInterval(60)
                              .setPrometheusAddress("0.0.0.0:9091")
                              // 配置全局参数
                              .setQueryTimeout(5000)
                              .setMaxConcurrentQueries(100)
                              .setIPv6Enabled(true)
                              .build();

        // 创建DNS解析器并应用配置
        DNSResolver resolver;
        if (!resolver.loadConfig(config)) {
            std::cerr << "Failed to apply configuration" << std::endl;
            return 1;
        }

        // 动态更新配置
        auto updated_config = config.clone();
        updated_config.setQueryTimeout(3000);
        updated_config.addServer(
                DNSServerConfigBuilder()
                        .setAddress("1.1.1.1")
                        .setPort(53)
                        .setWeight(1)
                        .setTimeout(2000)
                        .setEnabled(true)
                        .build());

        if (!resolver.loadConfig(updated_config)) {
            std::cerr << "Failed to update configuration" << std::endl;
            return 1;
        }

        // 使用解析器
        auto future = resolver.resolve("www.example.com");
        auto result = future.get();

        if (result.status == ARES_SUCCESS) {
            std::cout << "Resolution successful!" << std::endl;
            for (const auto &ip: result.ip_addresses) {
                std::cout << "IP: " << ip << std::endl;
            }
        } else {
            std::cerr << "Resolution failed: "
                      << ares_strerror(result.status) << std::endl;
        }

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}