#include "services/CacheService.hpp"
#include <chrono>
#include <iostream>
#include <iterator>
#include <vector>

namespace clipstream::services {

CacheService::CacheService(const std::string& host, uint16_t port) {
    sw::redis::ConnectionOptions opts;
    opts.host = host;
    opts.port = static_cast<int>(port);
    redis_ = std::make_unique<sw::redis::Redis>(opts);
    std::cout << "[Cache] Connected to Redis at " << host << ":" << port << "\n";
}

void CacheService::set(const std::string& key, const std::string& value, int ttl_seconds) {
    try {
        redis_->setex(key, ttl_seconds, value);
    } catch (const std::exception& e) {
        std::cerr << "[Cache] SET error: " << e.what() << "\n";
    }
}

std::optional<std::string> CacheService::get(const std::string& key) {
    try {
        auto val = redis_->get(key);
        if (val) return *val;
    } catch (const std::exception& e) {
        std::cerr << "[Cache] GET error: " << e.what() << "\n";
    }
    return std::nullopt;
}

void CacheService::del(const std::string& key) {
    try { redis_->del(key); }
    catch (const std::exception& e) {
        std::cerr << "[Cache] DEL error: " << e.what() << "\n";
    }
}

void CacheService::invalidate_pattern(const std::string& pattern) {
    try {
        auto cursor = 0LL;
        do {
            std::vector<std::string> keys;
            cursor = redis_->scan(cursor, pattern, 100, std::back_inserter(keys));
            for (const auto& key : keys) redis_->del(key);
        } while (cursor != 0);
    } catch (const std::exception& e) {
        std::cerr << "[Cache] INVALIDATE error: " << e.what() << "\n";
    }
}

bool CacheService::ping_redis() {
    try {
        if (!redis_) return false;
        return redis_->ping() == "PONG";
    } catch (const std::exception& e) {
        std::cerr << "[Cache] PING error: " << e.what() << "\n";
        return false;
    }
}

bool CacheService::try_set_nx(const std::string& key, const std::string& value, int ttl_seconds) {
    try {
        if (!redis_) return true;
        const auto ttl_ms = std::chrono::milliseconds(static_cast<long long>(ttl_seconds) * 1000LL);
        return redis_->set(key, value, ttl_ms, sw::redis::UpdateType::NOT_EXIST);
    } catch (const std::exception& e) {
        std::cerr << "[Cache] try_set_nx: " << e.what() << "\n";
        return true;
    }
}

} // namespace clipstream::services
