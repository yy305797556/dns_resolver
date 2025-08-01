#include "DNSEvent.h"
#include <iostream>

DNSEventManager &DNSEventManager::getInstance() {
    static DNSEventManager instance;
    return instance;
}

void DNSEventManager::registerListener(const std::shared_ptr<DnsEventListener> &listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_[listener->getName()] = listener;
}

void DNSEventManager::unregisterListener(const std::string &listener_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.erase(listener_name);
}

void DNSEventManager::addCallback(const std::string &name, const DNSEventCallback &callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_[name] = callback;
}

void DNSEventManager::removeCallback(const std::string &name) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.erase(name);
}

void DNSEventManager::notifyAddressChanged(const DNSAddressEvent &event) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 通知所有监听器
    for (const auto &[name, listener]: listeners_) {
        try {
            listener->onAddressChanged(event);
        } catch (const std::exception &e) {
            std::cerr << "Error notifying listener " << name
                      << ": " << e.what() << std::endl;
        }
    }

    // 执行所有回调
    for (const auto &[name, callback]: callbacks_) {
        try {
            callback(event);
        } catch (const std::exception &e) {
            std::cerr << "Error executing callback " << name
                      << ": " << e.what() << std::endl;
        }
    }
}

size_t DNSEventManager::getListenerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return listeners_.size() + callbacks_.size();
}