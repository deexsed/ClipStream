#pragma once

#include <optional>
#include <string>
#include <memory>
#include <sw/redis++/redis++.h>

namespace clipstream::services {

class CacheService {
public:
    CacheService(const std::string& host, uint16_t port);

    void set(const std::string& key, const std::string& value, int ttl_seconds = 300);
    std::optional<std::string> get(const std::string& key);
    void del(const std::string& key);
    void invalidate_pattern(const std::string& pattern);
    bool ping_redis();

    /// SET key value NX EX ttl — true, если ключ установлен (захват лока).
    bool try_set_nx(const std::string& key, const std::string& value, int ttl_seconds);

private:
    std::unique_ptr<sw::redis::Redis> redis_;
};

} // namespace clipstream::services
