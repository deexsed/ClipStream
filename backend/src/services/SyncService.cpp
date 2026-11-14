#include "services/SyncService.hpp"
#include "database/SqlEscape.hpp"
#include "repositories/SyncRepository.hpp"
#include "services/CacheService.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <mysql/mysql.h>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace clipstream::services {

namespace {

constexpr const char* kSyncLockKey = "clipstream:sync:lock";

class SyncRedisGuard {
  public:
    SyncRedisGuard(CacheService* cache, std::atomic<bool>& held) : cache_(cache), held_(held) {
        token_ = std::to_string(std::random_device{}()) + ":" + std::to_string(std::random_device{}());
        held_ = cache_ && cache_->try_set_nx(kSyncLockKey, token_, 60);
        if (held_)
            renewer_ = std::thread([this] {
                std::unique_lock lock(mutex_);
                while (!stop_) {
                    if (wake_.wait_for(lock, std::chrono::seconds(15), [this] { return stop_; }))
                        break;
                    try {
                        if (!cache_->compare_expire(kSyncLockKey, token_, 60)) {
                            held_ = false;
                            break;
                        }
                    } catch (...) {
                        held_ = false;
                        break;
                    }
                }
            });
    }
    bool acquired() const { return held_; }
    ~SyncRedisGuard() {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        if (renewer_.joinable())
            renewer_.join();
        held_ = false;
        if (cache_)
            try {
                cache_->compare_delete(kSyncLockKey, token_);
            } catch (...) {
            }
    }

  private:
    CacheService* cache_;
    std::atomic<bool>& held_;
    std::string token_;
    std::thread renewer_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_{};
};

} // namespace

const std::vector<GameSeed> SyncService::GAMES = {
    {"29595", "Dota 2"},
    {"32399", "Counter-Strike 2"},
    {"516575", "Valorant"},
};

SyncService::SyncService(TwitchService& twitch, repositories::ClipRepository& clip_repo,
                         database::ConnectionPool& db_pool, boost::asio::io_context& ioc, CacheService* cache)
    : twitch_(twitch), clip_repo_(clip_repo), db_pool_(db_pool), cache_(cache) {
    (void)ioc;
}

SyncService::~SyncService() { stop(); }
void SyncService::stop() {
    stopping_ = true;
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
    if (recommendation_worker_.joinable())
        recommendation_worker_.join();
}

void SyncService::seed_games() {
    database::ScopedConnection conn(db_pool_);
    for (auto& g : GAMES) {
        std::string sql =
            "INSERT IGNORE INTO games (twitch_id, name) VALUES ('" + g.twitch_id + "', '" + g.name + "')";
        database::query(conn.raw(), sql.c_str());
    }
    std::cout << "[Sync] Base games seeded\n";
}

void SyncService::sync_top_games() {
    if (!cache_)
        return;
    const auto now = std::time(nullptr);
    if (auto due = cache_->get("clipstream:sync:v3:discovery_due"); due && std::stoll(*due) > now)
        return;
    const auto cursor = cache_->get("clipstream:sync:v3:category_cursor").value_or("");
    unsigned requests = 0;
    TwitchService::BackgroundScope background(requests);
    repositories::SyncRepository repo(db_pool_);
    try {
        auto page = twitch_.fetch_top_games_page(100, cursor);
        {
            database::ScopedConnection conn(db_pool_);
            for (const auto& g : page.games) {
                auto esc = [&](const std::string& v) { return database::escape(conn.raw(), v); };
                const auto sql = "INSERT INTO games(twitch_id,name,box_art) VALUES('" + esc(g.twitch_id) +
                                 "','" + esc(g.name) + "','" + esc(g.box_art_url) +
                                 "') ON DUPLICATE KEY UPDATE "
                                 "name=VALUES(name),box_art=VALUES(box_art)";
                database::query(conn.raw(), sql.c_str());
            }
        }
        const auto depth = std::stoi(cache_->get("clipstream:sync:v3:category_depth").value_or("0")) + 1;
        cache_->set_required("clipstream:sync:v3:category_cursor", depth >= 10 ? "" : page.cursor, 86400);
        cache_->set_required("clipstream:sync:v3:category_depth",
                             std::to_string(depth >= 10 || page.cursor.empty() ? 0 : depth), 86400);
        cache_->set_required("clipstream:sync:v3:discovery_due", std::to_string(now + 300), 86400);
        repo.record_discovery("ok", requests);
        std::cout << "[Sync] Discovered " << page.games.size() << " categories\n";
    } catch (const TwitchError& e) {
        cache_->set_required("clipstream:sync:v3:discovery_due",
                             std::to_string(now + std::max(60, e.retry_after)), 86400);
        if (e.upstream_status == 400)
            cache_->del("clipstream:sync:v3:category_cursor");
        repo.record_discovery(e.status == 429 || e.status == 503 ? "deferred" : "error", requests,
                              e.upstream_status);
        // Existing categories remain usable while discovery is unavailable.
    }
}

static std::string utc_iso(std::chrono::system_clock::time_point tp) {
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

void SyncService::sync_clips_all_phases() {
    repositories::SyncRepository repo(db_pool_);
    repo.schedule(std::time(nullptr), interval_minutes_ * 60);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    bool history_deferred = std::chrono::steady_clock::now() < history_resume_at_;
    bool changed = false, budget_paused = false;
    struct PageJob {
        repositories::SyncWindow window;
        unsigned requests{};
        std::future<FetchResult> result;
    };
    // Only HTTP fetches overlap. One coordinator selects distinct windows and commits
    // pages serially under the renewable worker lease and durable checkpoint checks.
    for (int dispatched = 0; dispatched < 30 && !stopping_ && lease_held_ && !budget_paused &&
                             std::chrono::steady_clock::now() < deadline;) {
        const bool catching_up = repo.fresh_catching_up();
        std::vector<std::unique_ptr<PageJob>> jobs;
        std::vector<uint64_t> excluded;
        for (int lane = 0; lane < 2 && dispatched < 30 && !stopping_ && lease_held_ &&
                           std::chrono::steady_clock::now() < deadline;
             ++lane) {
            const auto turn = polling_turn_;
            polling_turn_ = (polling_turn_ + 1) % 20;
            // Once eligible windows are five minutes behind, spend three quarters
            // of fresh slots on oldest work; keep live-head and history progress.
            const bool history_slot = (catching_up ? turn == 19 : turn % 10 == 9) && !history_deferred;
            const bool oldest = catching_up ? turn % 4 != 0 : turn % 4 == 3;
            auto window = repo.next(history_slot ? "history" : "fresh", oldest, 0, excluded);
            if (!window && history_slot)
                window = repo.next("fresh", oldest, 0, excluded);
            if (!window && !history_deferred)
                window = repo.next("history", false, 0, excluded);
            if (!window) {
                if (jobs.empty())
                    retry_delay_seconds_ = 5;
                break;
            }
            auto job = std::make_unique<PageJob>();
            job->window = std::move(*window);
            auto* page = job.get();
            job->result = std::async(std::launch::async, [this, page] {
                TwitchService::BackgroundScope background(page->requests, page->window.phase == "history");
                const auto& w = page->window;
                return twitch_.fetch_clips(w.twitch_id, 100, w.cursor,
                                           utc_iso(std::chrono::system_clock::from_time_t(w.start)),
                                           utc_iso(std::chrono::system_clock::from_time_t(w.end)),
                                           repositories::SyncRepository::minimum_views(w.phase), true);
            });
            excluded.push_back(job->window.id);
            jobs.push_back(std::move(job));
            ++dispatched;
        }
        if (jobs.empty())
            break;
        for (auto& job : jobs) {
            auto* window = &job->window;
            unsigned requests = 0;
            try {
                auto result = job->result.get();
                requests = job->requests;
                if (stopping_ || !lease_held_)
                    return;
                if (!window->cursor.empty() && result.cursor == window->cursor)
                    throw TwitchError(502, "Repeated clip cursor", 60, 400);
                repo.commit_page(*window, result, requests, interval_minutes_ * 60);
                changed = changed || !result.clips.empty();
                if (cache_) {
                    // Update at most once per broadcaster per page, after the durable page
                    // commit.
                    std::unordered_map<std::string, models::Clip> latest;
                    for (const auto& clip : result.clips) {
                        auto& old = latest[clip.broadcaster_id];
                        if (clip.twitch_created_at > old.twitch_created_at)
                            old = clip;
                    }
                    for (const auto& [id, clip] : latest) {
                        if (id.empty())
                            continue;
                        const auto key = "eventsub:clip_latest:" + id;
                        std::string previous;
                        if (auto raw = cache_->get(key)) {
                            auto saved = nlohmann::json::parse(*raw, nullptr, false);
                            if (saved.is_object() && saved.contains("clip") && saved["clip"].is_object())
                                previous = saved["clip"].value("twitch_created_at", "");
                        }
                        if (clip.twitch_created_at >= previous)
                            cache_->set(key, nlohmann::json({{"broadcaster_id", id}, {"clip", clip}}).dump(),
                                        86400);
                    }
                }
                std::cout << "[Sync] " << window->phase << " game=" << window->twitch_id
                          << " fetched=" << result.raw_count << " accepted=" << result.clips.size()
                          << " requests=" << requests << "\n";
            } catch (const TwitchError& e) {
                // get() has joined this fetch, including when it reports an exception.
                if (stopping_ || !lease_held_)
                    continue;
                repo.fail(*window, e, job->requests);
                if (e.history_budget) {
                    history_deferred = true;
                    history_resume_at_ =
                        std::chrono::steady_clock::now() + std::chrono::seconds(e.retry_after + 1);
                } else if (e.status == 429) {
                    retry_delay_seconds_ = std::clamp(e.retry_after + 1, 1, 3600);
                    budget_paused = true;
                }
            }
        }
    }
    if (cache_ && changed) {
        cache_->invalidate_pattern("clips:*");
        cache_->invalidate_pattern("home:*");
        cache_->invalidate_pattern("related:*");
    }
}

void SyncService::sync_now() {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    SyncRedisGuard guard(cache_, lease_held_);
    if (!guard.acquired()) {
        std::cout << "[Sync] Skipped periodic clip sync: lock held by another "
                     "backend instance\n";
        return;
    }
    sync_clips_all_phases();
}

void SyncService::run_startup_sync() {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    SyncRedisGuard guard(cache_, lease_held_);
    if (!guard.acquired()) {
        std::cout << "[Sync] Skipped startup sync: lock held by another backend "
                     "instance\n";
        return;
    }
    sync_top_games();
    sync_clips_all_phases();
}

void SyncService::start_periodic(int interval_minutes, bool poll_twitch) {
    if (worker_.joinable() || recommendation_worker_.joinable())
        return;
    interval_minutes_ = std::clamp(interval_minutes, 1, 1440);
    stopping_ = false;
    recommendation_worker_ = std::thread([this] {
        uint64_t catalog_cursor = 0, user_cursor = 0;
        while (!stopping_) {
            try {
                // Separate owner lease: Twitch polling cannot starve feature
                // computation.
                const std::string owner = "reco-" + std::to_string(std::random_device{}()) + ":" +
                                          std::to_string(std::random_device{}());
                const std::string key = "clipstream:recommendations:worker";
                if (!cache_ || cache_->try_set_nx(key, owner, 120)) {
                    try {
                        if (catalog_cursor >= clip_repo_.recommendation_catalog_max_id())
                            catalog_cursor = 0;
                        clip_repo_.refresh_dirty_recommendation_features();
                        catalog_cursor = clip_repo_.refresh_recommendation_features(catalog_cursor, 1000);
                        database::ScopedConnection conn(db_pool_);
                        const auto q = "SELECT user_id FROM recommendation_active_users "
                                       "WHERE user_id>" +
                                       std::to_string(user_cursor) +
                                       " AND touched_at>UTC_TIMESTAMP()-INTERVAL 30 DAY "
                                       "ORDER BY user_id LIMIT 20";
                        database::query(conn.raw(), q.c_str());
                        database::Result res(mysql_store_result(conn.raw()));
                        std::vector<uint64_t> users;
                        while (auto row = mysql_fetch_row(res))
                            users.push_back(std::stoull(row[0]));
                        res.reset();
                        conn.release();
                        if (users.empty())
                            user_cursor = 0;
                        for (auto user : users) {
                            if (stopping_)
                                break;
                            if (cache_ && !cache_->compare_expire(key, owner, 120))
                                break;
                            try {
                                clip_repo_.refresh_collaborative_predictions(user);
                            } catch (const std::exception&) {
                                std::cerr << "[Recommendations] Neighbor refresh failed for "
                                             "one profile\n";
                            }
                            user_cursor = user;
                        }
                        database::ScopedConnection cleanup(db_pool_);
                        database::query(cleanup.raw(),
                                        "DELETE FROM recommendation_deliveries WHERE "
                                        "served_at<UTC_TIMESTAMP()-INTERVAL 30 DAY LIMIT 5000");
                        database::query(cleanup.raw(),
                                        "DELETE FROM clip_twitch_snapshots WHERE "
                                        "observed_at<UTC_TIMESTAMP()-INTERVAL 7 DAY LIMIT 5000");
                    } catch (...) {
                        if (cache_)
                            cache_->compare_delete(key, owner);
                        throw;
                    }
                    if (cache_)
                        cache_->compare_delete(key, owner);
                }
            } catch (const std::exception&) {
                std::cerr << "[Recommendations] Feature batch failed; retrying next cycle\n";
            }
            std::unique_lock lock(wake_mutex_);
            wake_.wait_for(lock, std::chrono::seconds(30), [this] { return stopping_.load(); });
        }
    });
    if (!poll_twitch)
        return;
    worker_ = std::thread([this] {
        while (!stopping_) {
            retry_delay_seconds_ = 1;
            try {
                run_startup_sync();
            } catch (const TwitchError& error) {
                retry_delay_seconds_ = std::clamp(error.retry_after + 1, 5, 3600);
                std::cerr << "[Sync] Twitch unavailable (status " << error.status
                          << "); retrying on next cycle\n";
            } catch (const std::exception&) {
                retry_delay_seconds_ = 10;
                std::cerr << "[Sync] Cycle failed; retrying on next cycle\n";
            }
            std::unique_lock lock(wake_mutex_);
            wake_.wait_for(lock, std::chrono::seconds(retry_delay_seconds_),
                           [this] { return stopping_.load(); });
        }
    });
}

} // namespace clipstream::services
