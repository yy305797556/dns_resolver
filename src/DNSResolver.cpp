
#include "DNSResolver.h"
#include "DNSCachePersistor.h"
#include "DNSConfigValidator.h"
#include "DNSEvent.h"

#include <csignal>
#include <iostream>

DNSResolver::DNSResolver() {
    int status = ares_library_init(ARES_LIB_INIT_ALL);
    if (status != ARES_SUCCESS) {
        throw std::runtime_error("c-ares library initialization failed");
    }

    // 初始化指标收集器
    metrics_ = std::make_shared<DNSMetrics>();
}

DNSResolver::~DNSResolver() {
    if (initialized_) {
        // 保存缓存（如果配置了持久化）
        if (config_ && config_->cache().persistent) {
            [[maybe_unused]] auto ret = save_cache(config_->cache().cache_file);
        }

        // 清理资源
        ares_destroy(channel_);
        initialized_ = false;
    }
    ares_library_cleanup();
}

bool DNSResolver::init(const std::vector<std::string> &dns_servers, std::chrono::seconds cache_ttl) {
    ares_options options{};
    int optmask = 0;

    // 设置c-ares选项
    memset(&options, 0, sizeof(options));
    options.flags = ARES_FLAG_NOCHECKRESP;// 不检查响应的id
    options.timeout = 2000;               // 2秒超时
    options.tries = 3;                    // 重试3次
    options.ndots = 1;                    // 域名中的点数阈值
    options.sock_state_cb = socket_callback;
    options.sock_state_cb_data = this;
    optmask = ARES_OPT_FLAGS | ARES_OPT_TIMEOUT | ARES_OPT_TRIES | ARES_OPT_NDOTS | ARES_OPT_SOCK_STATE_CB;

    int status = ares_init_options(&channel_, &options, optmask);
    if (status != ARES_SUCCESS) {
        std::cerr << "Failed to initialize c-ares: " << ares_strerror(status) << std::endl;
        return false;
    }

    if (!dns_servers.empty()) {
        dns_server_list_ = dns_servers;
        std::ostringstream servers;
        servers << dns_servers[0];// 第一个元素不加逗号

        // 后续元素前加逗号
        for (size_t i = 1; i < dns_servers.size(); ++i) {
            servers << "," << dns_servers[i];
        }

        // 设置DNS服务器
        status = ares_set_servers_ports_csv(channel_, servers.str().c_str());

        if (status != ARES_SUCCESS) {
            std::cerr << "Failed to set DNS servers: " << ares_strerror(status) << std::endl;
            return false;
        }
    }

    cache_ = std::make_shared<DNSCache>(cache_ttl);
    initialized_ = true;
    return true;
}

bool DNSResolver::loadConfig(const DNSResolverConfig &config) {
    try {
        // 验证配置
        DNSConfigValidator::validate(config);
        // 获取启用的DNS服务器
        std::vector<std::string> active_servers;
        for (const auto &server: config.servers()) {
            if (server.enabled) {
                active_servers.push_back(server.address);
            }
        }
        // 重新初始化
        if (!init(active_servers, config.cache().ttl)) {
            return false;
        }
        // 配置指标收集
        if (config.metrics().enabled) {
            metrics_->startPrometheusExporter(config.metrics().prometheus_address);
        }
        // 加载持久化缓存
        if (config.cache().enabled && config.cache().persistent) {
            load_cache(config.cache().cache_file);
        }
        config_ = std::make_shared<DNSResolverConfig>(config);
    } catch (const ConfigValidationError &e) {
        std::cerr << "Configuration validation error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception &e) {
        std::cerr << "Error loading configuration: " << e.what() << std::endl;
        return false;
    }

    return true;
}

bool DNSResolver::loadConfig(const std::string &config_file) {
    try {
        auto &config = DNSResolverConfig::getInstance();
        if (!config.loadFromFile(config_file)) {
            return false;
        }
        return loadConfig(config);
    } catch (const std::exception &e) {
        std::cerr << "Error loading configuration file: " << e.what() << std::endl;
        return false;
    }
}

bool DNSResolver::reloadConfig() {
    if (!config_) {
        return false;
    }

    // 保存当前缓存
    if (config_->cache().enabled && config_->cache().persistent) {
        [[maybe_unused]] auto ret = save_cache(config_->cache().cache_file);
    }

    return loadConfig(config_->metrics().metrics_file);
}

std::future<DNSResolver::ResolveResult> DNSResolver::resolve(const std::string &hostname) {

    if (!initialized_) {
        std::promise<ResolveResult> promise;
        promise.set_value({ARES_ENOTINITIALIZED, hostname, {}});
        return promise.get_future();
    }

    // 检查缓存
    std::vector<std::string> cached_ips;
    if (cache_->get(hostname, cached_ips)) {
        metrics_->recordCacheHit(hostname);
        std::promise<ResolveResult> promise;
        ResolveResult result;
        result.hostname = hostname;
        result.ip_addresses = cached_ips;
        result.status = ARES_SUCCESS;
        result.resolution_time = std::chrono::milliseconds(0);
        promise.set_value(std::move(result));
        return promise.get_future();
    }

    metrics_->recordCacheMiss(hostname);

    auto context = new QueryContext{
            hostname,
            std::promise<ResolveResult>(),
            std::chrono::steady_clock::now(),
            std::vector<char>(512),
            shared_from_this()};

    struct ares_addrinfo_hints hints = {};
    hints.ai_family = config_ && config_->ipv6_enabled() ? AF_UNSPEC : AF_INET;
    hints.ai_flags = ARES_AI_CANONNAME;

    ares_getaddrinfo(channel_, hostname.c_str(), nullptr, &hints, addrinfo_callback, context);

    return context->promise.get_future();
}

std::vector<std::future<DNSResolver::ResolveResult>> DNSResolver::resolve_batch(const std::vector<std::string> &hostnames) {

    std::vector<std::future<ResolveResult>> results;
    results.reserve(hostnames.size());
    // 检查并应用并发限制
    size_t max_concurrent = config_ ? config_->max_concurrent_queries() : 100;
    size_t batch_size = std::min(hostnames.size(), max_concurrent);
    std::vector<std::string> current_batch;
    current_batch.reserve(batch_size);
    for (const auto &hostname: hostnames) {
        current_batch.push_back(hostname);
        if (current_batch.size() >= batch_size) {
            // 处理当前批次
            for (const auto &h: current_batch) {
                results.push_back(resolve(h));
            }
            // 等待当前批次完成
            wait_for_completion();
            current_batch.clear();
        }
    }

    // 处理剩余的主机名
    if (!current_batch.empty()) {
        for (const auto &h: current_batch) {
            results.push_back(resolve(h));
        }
        wait_for_completion();
    }

    return results;
}

std::future<DNSResolver::ResolveResult> DNSResolver::refresh(const std::string &hostname) {
    if (cache_) {
        cache_->remove(hostname);
    }
    return resolve(hostname);
}

void DNSResolver::clear_cache() {
    if (cache_) {
        cache_->clear();
    }
}

bool DNSResolver::save_cache(const std::string &filename) const {
    if (!cache_) {
        return false;
    }
    return DNSCachePersistor::save(*cache_, filename);
}

bool DNSResolver::load_cache(const std::string &filename) {
    if (!cache_) {
        return false;
    }
    return DNSCachePersistor::load(*cache_, filename);
}

std::shared_ptr<DNSCache> DNSResolver::getCache() const {
    return cache_;
}

std::shared_ptr<DNSMetrics> DNSResolver::getMetrics() const {
    return metrics_;
}

DNSMetrics::Stats DNSResolver::getStats() const {
    if (metrics_) {
        return metrics_->getStats();
    }
    return {};
}

void DNSResolver::socket_callback(void *data, ares_socket_t socket_fd, int readable, int writable) {
    // auto *resolver = static_cast<DNSResolver *>(data);
    sockaddr_storage addr{};
    socklen_t addr_len = sizeof(addr);
    char ipstr[INET6_ADDRSTRLEN] = {0};
    // 获取远端地址
    if (getpeername(socket_fd, reinterpret_cast<struct sockaddr *>(&addr), &addr_len) == 0) {
        void *src = nullptr;
        if (addr.ss_family == AF_INET) {
            src = &(reinterpret_cast<struct sockaddr_in *>(&addr)->sin_addr);
        } else if (addr.ss_family == AF_INET6) {
            src = &(reinterpret_cast<struct sockaddr_in6 *>(&addr)->sin6_addr);
        }

        if (src && inet_ntop(addr.ss_family, src, ipstr, sizeof(ipstr))) {
            // std::lock_guard<std::mutex> lock(resolver->mutex_);
            // resolver->socket_server_map_[socket_fd] = ipstr;
        }
    } else {
        std::cerr << "getpeername" << std::endl;
    }

    if (!readable && !writable) {
        // std::lock_guard<std::mutex> lock(resolver->mutex_);
        // resolver->socket_server_map_.erase(socket_fd);
    }
}

void DNSResolver::addrinfo_callback(void *arg, int status, int timeouts, struct ares_addrinfo *result) {
    auto *context = static_cast<QueryContext *>(arg);
    context->resolver->process_result(context, status, result);
    if (result) {
        ares_freeaddrinfo(result);
    }
    delete context;
}

void DNSResolver::process_result(QueryContext *context, int status, const ares_addrinfo *result) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - context->start_time);

    ResolveResult resolve_result;
    resolve_result.hostname = context->hostname;
    resolve_result.status = status;
    resolve_result.resolution_time = duration;

    std::vector<std::string> old_addresses;
    cache_->get(context->hostname, old_addresses);

    if (status == ARES_SUCCESS && result) {
        for (struct ares_addrinfo_node *node = result->nodes;
             node != nullptr;
             node = node->ai_next) {

            char ip[INET6_ADDRSTRLEN];
            const void *addr;

            if (node->ai_family == AF_INET) {
                const auto *addr_in = reinterpret_cast<struct sockaddr_in *>(node->ai_addr);
                addr = &(addr_in->sin_addr);
            } else if (node->ai_family == AF_INET6) {
                const auto *addr_in6 = reinterpret_cast<struct sockaddr_in6 *>(node->ai_addr);
                addr = &(addr_in6->sin6_addr);
            } else {
                continue;
            }

            if (inet_ntop(node->ai_family, addr, ip, sizeof(ip))) {
                resolve_result.ip_addresses.emplace_back(ip);
            }
        }

        // 更新缓存
        if (!resolve_result.ip_addresses.empty()) {
            cache_->update(context->hostname, resolve_result.ip_addresses);

            // 检查地址是否发生变化
            if (old_addresses != resolve_result.ip_addresses) {
                notifyAddressChange(context->hostname, old_addresses, resolve_result.ip_addresses, "query");
            }
        }
    } else {
        // 处理错误
        metrics_->recordError("resolution_failure", std::string(ares_strerror(status)));
        // 实施重试策略
        if (config_ && status != ARES_ENODATA && status != ARES_ENOTFOUND) {
            static uint32_t retry_count = 0;
            if (retry_count < config_->retry().max_attempts) {
                retry_count++;
                metrics_->recordRetry(context->hostname, retry_count);
                // 使用指数退避
                auto delay = config_->retry().base_delay_ms * (1 << (retry_count - 1));
                delay = std::min(delay, config_->retry().max_delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));

                // 重新发起查询
                ares_getaddrinfo(channel_, context->hostname.c_str(),
                                 nullptr, nullptr, addrinfo_callback, context);
                return;// 不要设置promise结果
            }
            retry_count = 0;// 重置重试计数
        }
    }

    metrics_->recordQuery(context->hostname, duration, status == ARES_SUCCESS);

    context->promise.set_value(std::move(resolve_result));
}

void DNSResolver::notifyAddressChange(const std::string &hostname, const std::vector<std::string> &old_addresses,
                                      const std::vector<std::string> &new_addresses, const std::string &source) {

    DNSAddressEvent event;
    event.hostname = hostname;
    event.old_addresses = old_addresses;
    event.new_addresses = new_addresses;
    event.timestamp = std::chrono::system_clock::now();
    event.source = source;
    if (config_) {
        event.ttl = config_->cache().ttl.count();
    }
    event.record_type = "A";       // 或 "AAAA" 取决于地址类型
    event.is_authoritative = false;// 需要从DNS响应中获取

    DNSEventManager::getInstance().notifyAddressChanged(event);
}

void DNSResolver::wait_for_completion() {
    while (true) {
        fd_set readers, writers;
        int nfds = 0;
        timeval tv{};

        FD_ZERO(&readers);
        FD_ZERO(&writers);

        tv.tv_sec = 0;
        tv.tv_usec = 100000;// 100ms timeout

        ares_fds(channel_, &readers, &writers);
        nfds = ares_fds(channel_, &readers, &writers);

        if (nfds == 0) {
            break;
        }

        if (select(nfds, &readers, &writers, nullptr, &tv) < 0) {
            break;
        }

        ares_process(channel_, &readers, &writers);
    }
}