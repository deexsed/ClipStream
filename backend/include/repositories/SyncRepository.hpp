#pragma once

#include "database/ConnectionPool.hpp"
#include "services/TwitchService.hpp"
#include <optional>
#include <string_view>
#include <vector>

namespace clipstream::repositories {

struct SyncWindow {
    uint64_t id{}, game_id{};
    std::string twitch_id, phase, cursor;
    int64_t start{}, end{};
    unsigned pages{}, raw_count{};
};

// Durable scheduling, page commits and import accounting share the MySQL
// transaction.
class SyncRepository {
  public:
    explicit SyncRepository(database::ConnectionPool& pool) : pool_(pool) {}
    static constexpr unsigned minimum_views(std::string_view phase) { return phase == "history" ? 50 : 5; }
    static unsigned fresh_poll_delay(unsigned base_seconds, unsigned empty_streak);
    void schedule(int64_t now, int interval_seconds);
    bool fresh_catching_up(int64_t now = 0);
    std::optional<SyncWindow> next(const std::string& phase, bool oldest = false, int64_t now = 0,
                                   const std::vector<uint64_t>& excluded = {});
    void commit_page(const SyncWindow& window, const services::FetchResult& result, unsigned requests,
                     unsigned base_interval_seconds = 60, int64_t now = 0);
    void fail(const SyncWindow& window, const services::TwitchError& error, unsigned requests);
    void record_discovery(const std::string& status, unsigned requests, int upstream_status = 0);
    nlohmann::json dashboard();

  private:
    database::ConnectionPool& pool_;
};
} // namespace clipstream::repositories
