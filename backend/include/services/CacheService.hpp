#pragma once

#include <memory>
#include <optional>
#include <string>
#include <sw/redis++/redis++.h>

namespace clipstream::services {

class CacheService {
  public:
    CacheService(const std::string &host, uint16_t port);

    void set(const std::string &key, const std::string &value, int ttl_seconds = 300);
    std::optional<std::string> get(const std::string &key);
    void del(const std::string &key);
    void invalidate_pattern(const std::string &pattern);
    bool ping_redis();
    void set_required(const std::string &key, const std::string &value, int ttl_seconds);
    std::optional<std::string> consume(const std::string &key);
    bool compare_expire(const std::string &key, const std::string &value, int ttl_seconds);
    bool compare_delete(const std::string &key, const std::string &value);
    void update_rate_limit(const std::string &key, int remaining, int reset_epoch, int limit);
    bool acquire_rate_slot(const std::string &key, int limit, int window_seconds);

    /// SET key value NX EX ttl — true, если ключ установлен (захват лока).
    bool try_set_nx(const std::string &key, const std::string &value, int ttl_seconds);

  private:
    std::unique_ptr<sw::redis::Redis> redis_;
};

} // namespace clipstream::services
