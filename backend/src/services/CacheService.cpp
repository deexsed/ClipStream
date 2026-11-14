#include "services/CacheService.hpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <vector>

namespace clipstream::services {

CacheService::CacheService(const std::string &host, uint16_t port) {
    sw::redis::ConnectionOptions opts;
    opts.host = host;
    opts.port = static_cast<int>(port);
    opts.connect_timeout = std::chrono::milliseconds(1000);
    opts.socket_timeout = std::chrono::milliseconds(1000);
    if (const char *password = std::getenv("REDIS_PASSWORD"))
        opts.password = password;
    if (const char *file = std::getenv("REDIS_PASSWORD_FILE")) {
        std::ifstream input(file);
        if (!input)
            throw std::runtime_error("Cannot read Redis password file");
        std::ostringstream content;
        content << input.rdbuf();
        opts.password = content.str();
        while (!opts.password.empty() && (opts.password.back() == '\n' || opts.password.back() == '\r'))
            opts.password.pop_back();
    }
    if (const char *username = std::getenv("REDIS_USERNAME"))
        opts.user = username;
    if (const char *tls = std::getenv("REDIS_TLS"); tls && std::string(tls) == "true") {
        opts.tls.enabled = true;
        opts.tls.sni = host;
        if (const char *ca = std::getenv("REDIS_CACERT"))
            opts.tls.cacert = ca;
    }
    sw::redis::ConnectionPoolOptions pool;
    pool.size = 8;
    pool.wait_timeout = std::chrono::milliseconds(1000);
    redis_ = std::make_unique<sw::redis::Redis>(opts, pool);
    std::cout << "[Cache] Connected to Redis at " << host << ":" << port << "\n";
}

void CacheService::set(const std::string &key, const std::string &value, int ttl_seconds) {
    try {
        redis_->setex(key, ttl_seconds, value);
    } catch (const std::exception &e) {
        std::cerr << "[Cache] SET error: " << e.what() << "\n";
    }
}

std::optional<std::string> CacheService::get(const std::string &key) {
    try {
        auto val = redis_->get(key);
        if (val)
            return *val;
    } catch (const std::exception &e) {
        std::cerr << "[Cache] GET error: " << e.what() << "\n";
    }
    return std::nullopt;
}

void CacheService::del(const std::string &key) {
    try {
        redis_->del(key);
    } catch (const std::exception &e) {
        std::cerr << "[Cache] DEL error: " << e.what() << "\n";
    }
}

void CacheService::invalidate_pattern(const std::string &pattern) {
    try {
        auto cursor = 0LL;
        do {
            std::vector<std::string> keys;
            cursor = redis_->scan(cursor, pattern, 100, std::back_inserter(keys));
            if (!keys.empty())
                redis_->del(keys.begin(), keys.end());
        } while (cursor != 0);
    } catch (const std::exception &e) {
        std::cerr << "[Cache] INVALIDATE error: " << e.what() << "\n";
    }
}

bool CacheService::ping_redis() {
    try {
        if (!redis_)
            return false;
        return redis_->ping() == "PONG";
    } catch (const std::exception &e) {
        std::cerr << "[Cache] PING error: " << e.what() << "\n";
        return false;
    }
}

bool CacheService::try_set_nx(const std::string &key, const std::string &value, int ttl_seconds) {
    try {
        if (!redis_)
            throw std::runtime_error("Redis unavailable");
        const auto ttl_ms = std::chrono::milliseconds(static_cast<long long>(ttl_seconds) * 1000LL);
        return redis_->set(key, value, ttl_ms, sw::redis::UpdateType::NOT_EXIST);
    } catch (const std::exception &e) {
        std::cerr << "[Cache] try_set_nx: " << e.what() << "\n";
        throw;
    }
}

void CacheService::set_required(const std::string &key, const std::string &value, int ttl) {
    redis_->setex(key, ttl, value);
}
std::optional<std::string> CacheService::consume(const std::string &key) {
    return redis_->eval<sw::redis::OptionalString>(
        "local v=redis.call('GET',KEYS[1]);if v then redis.call('DEL',KEYS[1]) end;return v", {key}, {});
}
bool CacheService::compare_expire(const std::string &key, const std::string &value, int ttl) {
    return redis_->eval<long long>("if redis.call('GET',KEYS[1])==ARGV[1] then return "
                                   "redis.call('EXPIRE',KEYS[1],ARGV[2]) else return 0 end",
                                   {key}, {value, std::to_string(ttl)}) == 1;
}
bool CacheService::compare_delete(const std::string &key, const std::string &value) {
    return redis_->eval<long long>("if redis.call('GET',KEYS[1])==ARGV[1] then return "
                                   "redis.call('DEL',KEYS[1]) else return 0 end",
                                   {key}, {value}) == 1;
}
bool CacheService::acquire_rate_slot(const std::string &key, int limit, int seconds) {
    const std::string script =
        "local now=tonumber(redis.call('TIME')[1]);local q=KEYS[1]..':quota';local "
        "reset=tonumber(redis.call('HGET',q,'reset') or '0');if reset>now then local "
        "rem=tonumber(redis.call('HGET',q,'remaining') or '0');if rem<=20 then return 0 "
        "end;redis.call('HINCRBY',q,'remaining',-1);return 1 end;local "
        "bucket=KEYS[1]..':'..math.floor(now/tonumber(ARGV[2]));local n=redis.call('INCR',bucket);if n==1 "
        "then redis.call('EXPIRE',bucket,tonumber(ARGV[2])+1) end;return n<=tonumber(ARGV[1]) and 1 or 0";
    return redis_->eval<long long>(script, {key}, {std::to_string(limit), std::to_string(seconds)}) == 1;
}
void CacheService::update_rate_limit(const std::string &key, int remaining, int reset, int limit) {
    const std::string script =
        "local q=KEYS[1]..':quota';local old=tonumber(redis.call('HGET',q,'reset') or '0');local "
        "target=tonumber(ARGV[2]);local rem=math.min(tonumber(ARGV[1]),tonumber(ARGV[3]));if target<old then "
        "return 0 end;if target==old then rem=math.min(rem,tonumber(redis.call('HGET',q,'remaining') or "
        "rem)) end;redis.call('HSET',q,'reset',target,'remaining',rem);local "
        "now=tonumber(redis.call('TIME')[1]);redis.call('EXPIRE',q,math.max(1,target-now+1));return 1";
    redis_->eval<long long>(script, {key},
                            {std::to_string(remaining), std::to_string(reset), std::to_string(limit)});
}

} // namespace clipstream::services
