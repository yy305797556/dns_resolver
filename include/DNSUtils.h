#pragma once
#include <chrono>

class DNSUtils {
public:
    static int64_t getTime() {
        // 获取当前时间点
        auto now = std::chrono::system_clock::now();
        // 计算自纪元开始的时间差（duration）
        // 转换为毫秒
        auto duration = now.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        return millis;
    }
};