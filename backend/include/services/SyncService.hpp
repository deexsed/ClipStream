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
    SyncService(TwitchService& twitch, repositories::ClipRepository& clip_repo,
                database::ConnectionPool& db_pool, boost::asio::io_context& ioc,
                CacheService* cache = nullptr);

    ~SyncService();
    void stop();
    void seed_games();
    void sync_top_games();
    void sync_now();
    /// Первый прогон после старта: top games + клипы (с Redis-lock, см.
    /// run_startup_sync в main).
    void run_startup_sync();
    void start_periodic(int interval_minutes = 1);

  private:
    void sync_clips_all_phases();
    void recompute_recommendations_periodic();

    TwitchService& twitch_;
    repositories::ClipRepository& clip_repo_;
    database::ConnectionPool& db_pool_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> lease_held_{false};
    std::condition_variable wake_;
    std::mutex wake_mutex_;
    int interval_minutes_{1};
    int retry_delay_seconds_{1};
    std::chrono::steady_clock::time_point history_resume_at_{};
    unsigned polling_turn_{0};
    std::chrono::steady_clock::time_point last_reco_recompute_{};
    std::mutex sync_mutex_;
    CacheService* cache_{nullptr};

    static const std::vector<GameSeed> GAMES;
};

} // namespace clipstream::services
