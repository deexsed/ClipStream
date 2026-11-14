#include "services/SyncService.hpp"
#include "database/SqlEscape.hpp"
#include "services/CacheService.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <mysql/mysql.h>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace clipstream::services {

namespace {

constexpr const char *kSyncLockKey = "clipstream:sync:lock";

class SyncRedisGuard {
  public:
    SyncRedisGuard(CacheService *cache, std::atomic<bool> &held) : cache_(cache), held_(held) {
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
    CacheService *cache_;
    std::atomic<bool> &held_;
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

SyncService::SyncService(TwitchService &twitch, repositories::ClipRepository &clip_repo,
                         database::ConnectionPool &db_pool, boost::asio::io_context &ioc, CacheService *cache)
    : twitch_(twitch), clip_repo_(clip_repo), db_pool_(db_pool), cache_(cache) {
    (void)ioc;
    last_reco_recompute_ = std::chrono::steady_clock::now() - std::chrono::hours(2);
}

SyncService::~SyncService() { stop(); }
void SyncService::stop() {
    stopping_ = true;
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

void SyncService::seed_games() {
    database::ScopedConnection conn(db_pool_);
    for (auto &g : GAMES) {
        std::string sql =
            "INSERT IGNORE INTO games (twitch_id, name) VALUES ('" + g.twitch_id + "', '" + g.name + "')";
        database::query(conn.raw(), sql.c_str());
    }
    std::cout << "[Sync] Base games seeded\n";
}

void SyncService::sync_top_games() {
    auto top_games = twitch_.fetch_top_games(30);
    database::ScopedConnection conn(db_pool_);

    for (auto &g : top_games) {
        auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };

        std::string sql = "INSERT INTO games (twitch_id, name, box_art) VALUES ('" + esc(g.twitch_id) +
                          "','" + esc(g.name) + "','" + esc(g.box_art_url) +
                          "') ON DUPLICATE KEY UPDATE name='" + esc(g.name) + "', box_art='" +
                          esc(g.box_art_url) + "'";
        database::query(conn.raw(), sql.c_str());
    }
    std::cout << "[Sync] Top games synced: " << top_games.size() << "\n";
}

std::vector<std::pair<uint64_t, std::string>> SyncService::get_all_games() {
    database::ScopedConnection conn(db_pool_);
    std::string sql = "SELECT id, twitch_id FROM games ORDER BY id";
    database::query(conn.raw(), sql.c_str());

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        throw database::Error(mysql_errno(conn.raw()));

    std::vector<std::pair<uint64_t, std::string>> games;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        games.emplace_back(std::stoull(row[0]), std::string(row[1]));
    }
    res.reset();
    return games;
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

namespace {

struct GameSyncState {
    uint64_t game_id{0};
    std::string twitch_id;
    std::string cursor;
    std::string started_at;
    std::string ended_at;
    bool finished{false};
    int upserted{0};
    int fetched{0};
};

} // namespace

int SyncService::sync_clips_for_period(const char *phase_label,
                                       const std::vector<std::pair<uint64_t, std::string>> &all_games,
                                       std::unordered_map<std::string, std::string> &avatar_cache,
                                       const std::string &started_at, const std::string &ended_at, int pages,
                                       int min_views) {
    int total = 0;

    // Каждый период — свой порядок игр.
    auto games = all_games;
    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(games.begin(), games.end(), rng);

    std::vector<GameSyncState> states;
    states.reserve(games.size());
    for (auto &g : games) {
        GameSyncState st;
        st.game_id = g.first;
        st.twitch_id = g.second;
        st.started_at = started_at;
        st.ended_at = ended_at;
        // A bounded cycle resumes the exact snapshot/cursor next time rather than restarting page one.
        if (cache_)
            if (auto progress =
                    cache_->get(std::string("clipstream:sync:cursor:") + phase_label + ":" + st.twitch_id)) {
                auto saved = nlohmann::json::parse(*progress, nullptr, false);
                if (saved.is_object()) {
                    st.cursor = saved.value("cursor", "");
                    st.started_at = saved.value("started_at", started_at);
                    st.ended_at = saved.value("ended_at", ended_at);
                }
            }
        states.push_back(std::move(st));
    }

    (void)avatar_cache;
    // Round-robin: за раунд по одной странице на каждую ещё активную игру — «горячая» игра не забирает 5
    // страниц подряд.
    for (int round = 0; round < pages; ++round) {
        bool any_active = false;
        for (auto &st : states) {
            if (stopping_ || !lease_held_)
                return total;
            if (st.finished)
                continue;
            any_active = true;

            auto result = twitch_.fetch_clips(st.twitch_id, 100, st.cursor, st.started_at, st.ended_at,
                                              min_views, true);
            st.fetched += static_cast<int>(result.clips.size());
            if (result.clips.empty() && result.cursor.empty()) {
                st.finished = true;
                if (cache_)
                    cache_->del(std::string("clipstream:sync:cursor:") + phase_label + ":" + st.twitch_id);
                continue;
            }
            if (!result.cursor.empty() && result.cursor == st.cursor) {
                if (cache_)
                    cache_->del(std::string("clipstream:sync:cursor:") + phase_label + ":" + st.twitch_id);
                throw TwitchError(502, "Twitch repeated a clip pagination cursor");
            }

            for (auto &clip : result.clips) {
                if (stopping_ || !lease_held_)
                    return total;
                clip.game_id = st.game_id;
                if (clip.embed_url.empty())
                    clip.embed_url = "https://clips.twitch.tv/embed?clip=" + clip.twitch_clip_id;
                // Upsert also refreshes views, title, language, creator, VOD and conservative maturity.
                clip_repo_.create(clip);
                ++st.upserted;
                if (cache_ && !clip.broadcaster_id.empty()) {
                    const auto key = "eventsub:clip_latest:" + clip.broadcaster_id;
                    std::string previous_time;
                    if (auto raw = cache_->get(key)) {
                        auto previous = nlohmann::json::parse(*raw, nullptr, false);
                        if (previous.is_object() && previous.contains("clip") && previous["clip"].is_object())
                            previous_time = previous["clip"].value("twitch_created_at", "");
                    }
                    if (clip.twitch_created_at >= previous_time)
                        cache_->set(
                            key,
                            nlohmann::json({{"broadcaster_id", clip.broadcaster_id}, {"clip", clip}}).dump(),
                            86400);
                }
            }

            st.cursor = result.cursor;
            if (st.cursor.empty())
                st.finished = true;
            if (cache_) {
                const auto key = std::string("clipstream:sync:cursor:") + phase_label + ":" + st.twitch_id;
                if (st.finished)
                    cache_->del(key);
                else
                    cache_->set_required(
                        key,
                        nlohmann::json(
                            {{"cursor", st.cursor}, {"started_at", st.started_at}, {"ended_at", st.ended_at}})
                            .dump(),
                        7 * 86400);
            }
        }
        if (!any_active)
            break;
    }

    for (auto &st : states) {
        std::cout << "[Sync] [" << (phase_label ? phase_label : "?") << "] Game " << st.twitch_id << " ("
                  << started_at << "): fetched " << st.fetched << ", upserted " << st.upserted
                  << " (min_views=" << min_views << ")\n";
        total += st.upserted;
    }
    return total;
}

void SyncService::sync_clips_all_phases() {
    std::cout << "[Sync] Starting clip sync from Twitch...\n";
    auto all_games = get_all_games();
    std::unordered_map<std::string, std::string> avatar_cache;
    int total = 0;

    auto now = std::chrono::system_clock::now();

    // 1) Fresh clips: last 24 hours, low view threshold (2+), больше страниц — чаще подхватываем новые клипы
    {
        auto day_ago = now - std::chrono::hours(24);
        std::string sa = utc_iso(day_ago);
        std::string ea = utc_iso(now);
        int n = sync_clips_for_period("today", all_games, avatar_cache, sa, ea, 8, 2);
        total += n;
        std::cout << "[Sync] Today clips: " << n << " upserted\n";
    }

    if (stopping_ || !lease_held_)
        return;
    // 2) Last 7 days, threshold 10+, 5 pages
    {
        auto week_ago = now - std::chrono::hours(24 * 7);
        std::string sa = utc_iso(week_ago);
        std::string ea = utc_iso(now);
        int n = sync_clips_for_period("week", all_games, avatar_cache, sa, ea, 5, 10);
        total += n;
        std::cout << "[Sync] Week clips: " << n << " upserted\n";
    }

    if (stopping_ || !lease_held_)
        return;
    // 3) Last 30 days, threshold 30+, 5 pages
    {
        auto month_ago = now - std::chrono::hours(24 * 30);
        std::string sa = utc_iso(month_ago);
        std::string ea = utc_iso(now);
        int n = sync_clips_for_period("month", all_games, avatar_cache, sa, ea, 5, 30);
        total += n;
        std::cout << "[Sync] Month clips: " << n << " upserted\n";
    }

    if (stopping_ || !lease_held_)
        return;
    // 4) All time popular, threshold 50+, 10 pages (no date filter)
    {
        int n = sync_clips_for_period("all", all_games, avatar_cache, "", "", 10, 50);
        total += n;
        std::cout << "[Sync] All-time clips: " << n << " upserted\n";
    }

    std::cout << "[Sync] Done. Total upserted clips: " << total << "\n";
    if (stopping_ || !lease_held_)
        return;
    if (cache_) {
        cache_->invalidate_pattern("clips:*");
        cache_->invalidate_pattern("clip:*");
        cache_->invalidate_pattern("home:*");
        cache_->invalidate_pattern("related:*");
    }
    recompute_recommendations_periodic();
}

void SyncService::sync_now() {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    SyncRedisGuard guard(cache_, lease_held_);
    if (!guard.acquired()) {
        std::cout << "[Sync] Skipped periodic clip sync: lock held by another backend instance\n";
        return;
    }
    sync_clips_all_phases();
}

void SyncService::run_startup_sync() {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    SyncRedisGuard guard(cache_, lease_held_);
    if (!guard.acquired()) {
        std::cout << "[Sync] Skipped startup sync: lock held by another backend instance\n";
        return;
    }
    sync_top_games();
    sync_clips_all_phases();
}

void SyncService::start_periodic(int interval_minutes) {
    if (worker_.joinable())
        return;
    interval_minutes_ = std::clamp(interval_minutes, 1, 1440);
    stopping_ = false;
    worker_ = std::thread([this] {
        while (!stopping_) {
            try {
                run_startup_sync();
            } catch (const TwitchError &error) {
                std::cerr << "[Sync] Twitch unavailable (status " << error.status
                          << "); retrying on next cycle\n";
            } catch (const std::exception &) {
                std::cerr << "[Sync] Cycle failed; retrying on next cycle\n";
            }
            std::unique_lock lock(wake_mutex_);
            wake_.wait_for(lock, std::chrono::minutes(interval_minutes_),
                           [this] { return stopping_.load(); });
        }
    });
}

void SyncService::recompute_recommendations_periodic() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_reco_recompute_ < std::chrono::hours(1))
        return;
    last_reco_recompute_ = now;
    try {
        auto users = clip_repo_.get_recent_active_user_ids(30, 300);
        for (auto user_id : users) {
            auto similar = clip_repo_.get_similar_broadcasters(user_id, 12);
            if (!similar.empty())
                clip_repo_.upsert_user_recommendations(user_id, similar);
        }
        std::cout << "[Sync] user_recommendations recomputed for active users\n";
    } catch (...) {
        std::cerr << "[Sync] user_recommendations recompute failed\n";
    }
    if (clip_repo_.refresh_feature_store_clips()) {
        std::cout << "[Sync] feature_store_clip refreshed\n";
    } else {
        std::cerr << "[Sync] feature_store_clip refresh failed\n";
    }
    if (clip_repo_.refresh_feature_store_users()) {
        std::cout << "[Sync] feature_store_user refreshed\n";
    } else {
        std::cerr << "[Sync] feature_store_user refresh failed\n";
    }
}

} // namespace clipstream::services
