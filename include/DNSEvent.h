#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

struct DNSAddressEvent {
    std::string hostname;
    std::vector<std::string> old_addresses;
    std::vector<std::string> new_addresses;
    std::chrono::system_clock::time_point timestamp;
    std::string source;
    uint32_t ttl;
    std::string record_type;// "A", "AAAA", etc.
    bool is_authoritative;
};

using DNSEventCallback = std::function<void(const DNSAddressEvent &)>;

class DnsEventListener {
public:
    virtual ~DnsEventListener() = default;
    virtual void onAddressChanged(const DNSAddressEvent &event) = 0;
    [[nodiscard]] virtual std::string getName() const = 0;

    [[nodiscard]] virtual bool isEnabled() const { return enabled_; }
    virtual void setEnabled(bool enabled) { enabled_ = enabled; }

protected:
    bool enabled_ = true;
};

class DNSEventManager {
public:
    static DNSEventManager &getInstance();

    void registerListener(const std::shared_ptr<DnsEventListener> &listener);
    void unregisterListener(const std::string &listener_name);
    void addCallback(const std::string &name, const DNSEventCallback &callback);
    void removeCallback(const std::string &name);
    void notifyAddressChanged(const DNSAddressEvent &event);
    size_t getListenerCount() const;

    void enableListener(const std::string &listener_name);
    void disableListener(const std::string &listener_name);
    void pauseEvents();
    void resumeEvents();
    void clearEventQueue();

    // 事件过滤
    void addEventFilter(const DNSEventCallback &filter);
    void removeEventFilter(const std::string &filter_name);

private:
    DNSEventManager() = default;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<DnsEventListener>> listeners_;
    std::unordered_map<std::string, DNSEventCallback> callbacks_;

    bool paused_;
    std::queue<DNSAddressEvent> event_queue_;
    std::unordered_map<std::string, std::function<bool(const DNSAddressEvent &)>> filters_;

    void processEventQueue();
    bool shouldProcessEvent(const DNSAddressEvent &event) const;
};