#pragma once

#include "database/ConnectionPool.hpp"
#include "repositories/ClipRepository.hpp"
#include "services/TwitchService.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace clipstream::services {

struct GameSeed {
    std::string twitch_id;
    std::string name;
};

class CacheService;

class SyncService {
  public:
    SyncService(TwitchService &twitch, repositories::ClipRepository &clip_repo,
                database::ConnectionPool &db_pool, boost::asio::io_context &ioc,
                CacheService *cache = nullptr);

    ~SyncService();
    void stop();
    void seed_games();
    void sync_top_games();
    void sync_now();
    /// Первый прогон после старта: top games + клипы (с Redis-lock, см. run_startup_sync в main).
    void run_startup_sync();
    void start_periodic(int interval_minutes = 5);

  private:
    std::vector<std::pair<uint64_t, std::string>> get_all_games();

    int sync_clips_for_period(const char *phase_label,
                              const std::vector<std::pair<uint64_t, std::string>> &all_games,
                              std::unordered_map<std::string, std::string> &avatar_cache,
                              const std::string &started_at, const std::string &ended_at, int pages,
                              int min_views);
    void recompute_recommendations_periodic();
    void sync_clips_all_phases();

    TwitchService &twitch_;
    repositories::ClipRepository &clip_repo_;
    database::ConnectionPool &db_pool_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> lease_held_{false};
    std::condition_variable wake_;
    std::mutex wake_mutex_;
    int interval_minutes_{5};
    std::chrono::steady_clock::time_point last_reco_recompute_{};
    std::mutex sync_mutex_;
    CacheService *cache_{nullptr};

    static const std::vector<GameSeed> GAMES;
};

} // namespace clipstream::services
