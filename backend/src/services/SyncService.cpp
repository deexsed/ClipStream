#include "services/SyncService.hpp"
#include "services/CacheService.hpp"
#include "database/SqlEscape.hpp"
#include <iostream>
#include <mysql/mysql.h>
#include <unordered_map>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>
#include <random>
#include <mutex>
#include <thread>

namespace clipstream::services {

namespace {

constexpr const char* kSyncLockKey = "clipstream:sync:lock";

class SyncRedisGuard {
public:
    explicit SyncRedisGuard(CacheService* cache) : cache_(cache) {
        if (!cache_) {
            acquired_ = true;
            return;
        }
        std::ostringstream tok;
        tok << std::this_thread::get_id();
        acquired_ = cache_->try_set_nx(kSyncLockKey, tok.str(), 1200);
    }
    bool acquired() const { return acquired_; }
    ~SyncRedisGuard() {
        if (cache_ && acquired_) cache_->del(kSyncLockKey);
    }

private:
    CacheService* cache_{nullptr};
    bool acquired_{false};
};

} // namespace

const std::vector<GameSeed> SyncService::GAMES = {
    {"29595",  "Dota 2"},
    {"32399",  "Counter-Strike 2"},
    {"516575", "Valorant"},
};

SyncService::SyncService(TwitchService& twitch,
                         repositories::ClipRepository& clip_repo,
                         database::ConnectionPool& db_pool,
                         boost::asio::io_context& ioc,
                         CacheService* cache)
    : twitch_(twitch), clip_repo_(clip_repo), db_pool_(db_pool), timer_(ioc), cache_(cache) {
    last_reco_recompute_ = std::chrono::steady_clock::now() - std::chrono::hours(2);
}

void SyncService::seed_games() {
    database::ScopedConnection conn(db_pool_);
    for (auto& g : GAMES) {
        std::string sql =
            "INSERT IGNORE INTO games (twitch_id, name) VALUES ('"
            + g.twitch_id + "', '" + g.name + "')";
        mysql_query(conn.raw(), sql.c_str());
    }
    std::cout << "[Sync] Base games seeded\n";
}

void SyncService::sync_top_games() {
    auto top_games = twitch_.fetch_top_games(30);
    database::ScopedConnection conn(db_pool_);

    for (auto& g : top_games) {
        auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };

        std::string sql = "INSERT INTO games (twitch_id, name, box_art) VALUES ('"
            + esc(g.twitch_id) + "','" + esc(g.name) + "','" + esc(g.box_art_url)
            + "') ON DUPLICATE KEY UPDATE name='" + esc(g.name)
            + "', box_art='" + esc(g.box_art_url) + "'";
        mysql_query(conn.raw(), sql.c_str());
    }
    std::cout << "[Sync] Top games synced: " << top_games.size() << "\n";
}

uint64_t SyncService::ensure_game(const std::string& twitch_id) {
    database::ScopedConnection conn(db_pool_);
    std::string sql = "SELECT id FROM games WHERE twitch_id='" + twitch_id + "'";
    if (mysql_query(conn.raw(), sql.c_str())) return 0;

    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return 0;

    MYSQL_ROW row = mysql_fetch_row(res);
    uint64_t id = row ? std::stoull(row[0]) : 0;
    mysql_free_result(res);
    return id;
}

std::vector<std::pair<uint64_t, std::string>> SyncService::get_all_games() {
    database::ScopedConnection conn(db_pool_);
    std::string sql = "SELECT id, twitch_id FROM games ORDER BY id";
    if (mysql_query(conn.raw(), sql.c_str())) return {};

    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return {};

    std::vector<std::pair<uint64_t, std::string>> games;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        games.emplace_back(std::stoull(row[0]), std::string(row[1]));
    }
    mysql_free_result(res);
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
    bool finished{false};
    int inserted{0};
    int fetched{0};
};

} // namespace

int SyncService::sync_clips_for_period(
    const char* phase_label,
    const std::vector<std::pair<uint64_t, std::string>>& all_games,
    std::unordered_map<std::string, std::string>& avatar_cache,
    const std::string& started_at,
    const std::string& ended_at,
    int pages,
    int min_views)
{
    int total = 0;

    // Каждый период — свой порядок игр.
    auto games = all_games;
    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(games.begin(), games.end(), rng);

    std::vector<GameSyncState> states;
    states.reserve(games.size());
    for (auto& g : games) {
        GameSyncState st;
        st.game_id = g.first;
        st.twitch_id = g.second;
        states.push_back(std::move(st));
    }

    auto resolve_avatar = [this, &avatar_cache](const std::string& broadcaster_id, const std::string& broadcaster_name) -> std::string {
        const std::string avatar_key = !broadcaster_id.empty() ? broadcaster_id : broadcaster_name;
        if (avatar_key.empty()) return "";
        auto it = avatar_cache.find(avatar_key);
        if (it != avatar_cache.end()) return it->second;
        std::optional<TwitchUser> user;
        if (!broadcaster_id.empty()) user = twitch_.fetch_user_by_id(broadcaster_id);
        if (!user && !broadcaster_name.empty()) user = twitch_.fetch_user_by_login(broadcaster_name);
        std::string url = user ? user->profile_image_url : "";
        avatar_cache[avatar_key] = url;
        return url;
    };

    // Round-robin: за раунд по одной странице на каждую ещё активную игру — «горячая» игра не забирает 5 страниц подряд.
    for (int round = 0; round < pages; ++round) {
        bool any_active = false;
        for (auto& st : states) {
            if (st.finished) continue;
            any_active = true;

            auto result =
                twitch_.fetch_clips(st.twitch_id, 100, st.cursor, started_at, ended_at, min_views, true);
            st.fetched += static_cast<int>(result.clips.size());
            if (result.clips.empty()) {
                st.finished = true;
                continue;
            }

            std::vector<std::string> batch_ids;
            batch_ids.reserve(result.clips.size());
            for (auto& c : result.clips) {
                if (!c.twitch_clip_id.empty()) batch_ids.push_back(c.twitch_clip_id);
            }
            auto existing_map = clip_repo_.find_existing_twitch_clip_refs(batch_ids);
            int page_inserted = 0;

            for (auto& clip : result.clips) {
                auto ex_it = existing_map.find(clip.twitch_clip_id);
                if (ex_it != existing_map.end()) {
                    const auto& ex = ex_it->second;
                    std::string avatar_url;
                    const bool need_user_fetch =
                        ex.broadcaster_avatar.empty() && (!clip.broadcaster_id.empty() || !clip.broadcaster_name.empty());
                    if (need_user_fetch) avatar_url = resolve_avatar(clip.broadcaster_id, clip.broadcaster_name);

                    if ((!ex.broadcaster_id.empty() || !clip.broadcaster_id.empty())
                        || (ex.broadcaster_avatar.empty() && !avatar_url.empty())) {
                        clip_repo_.update_broadcaster_metadata(
                            ex.id,
                            clip.broadcaster_id.empty() ? ex.broadcaster_id : clip.broadcaster_id,
                            avatar_url.empty() ? ex.broadcaster_avatar : avatar_url
                        );
                    }
                    continue;
                }

                const std::string avatar_url = resolve_avatar(clip.broadcaster_id, clip.broadcaster_name);

                clip.game_id = st.game_id;
                clip.broadcaster_avatar = avatar_url;

                if (clip.embed_url.empty() && !clip.twitch_clip_id.empty())
                    clip.embed_url = "https://clips.twitch.tv/embed?clip=" + clip.twitch_clip_id;

                if (clip.thumbnail_url.empty())
                    clip.thumbnail_url = "https://static-cdn.jtvnw.net/previews-ttv/live_user_"
                                         + clip.broadcaster_name + "-320x180.jpg";

                try {
                    clip_repo_.create(clip);
                    ++st.inserted;
                    ++page_inserted;
                } catch (const std::exception& e) {
                    std::cerr << "[Sync] Insert clip error: " << e.what() << "\n";
                }
            }

            st.cursor = result.cursor;
            if (st.cursor.empty() || page_inserted == 0) st.finished = true;
        }
        if (!any_active) break;
    }

    for (auto& st : states) {
        std::cout << "[Sync] [" << (phase_label ? phase_label : "?") << "] Game " << st.twitch_id << " (" << started_at
                  << "): fetched " << st.fetched << ", inserted " << st.inserted << " new (min_views=" << min_views << ")\n";
        total += st.inserted;
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
        std::cout << "[Sync] Today clips: " << n << " new\n";
    }

    // 2) Last 7 days, threshold 10+, 5 pages
    {
        auto week_ago = now - std::chrono::hours(24 * 7);
        std::string sa = utc_iso(week_ago);
        std::string ea = utc_iso(now);
        int n = sync_clips_for_period("week", all_games, avatar_cache, sa, ea, 5, 10);
        total += n;
        std::cout << "[Sync] Week clips: " << n << " new\n";
    }

    // 3) Last 30 days, threshold 30+, 5 pages
    {
        auto month_ago = now - std::chrono::hours(24 * 30);
        std::string sa = utc_iso(month_ago);
        std::string ea = utc_iso(now);
        int n = sync_clips_for_period("month", all_games, avatar_cache, sa, ea, 5, 30);
        total += n;
        std::cout << "[Sync] Month clips: " << n << " new\n";
    }

    // 4) All time popular, threshold 50+, 10 pages (no date filter)
    {
        int n = sync_clips_for_period("all", all_games, avatar_cache, "", "", 10, 50);
        total += n;
        std::cout << "[Sync] All-time clips: " << n << " new\n";
    }

    std::cout << "[Sync] Done. Total new clips: " << total << "\n";
    recompute_recommendations_periodic();
}

void SyncService::sync_now() {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    SyncRedisGuard guard(cache_);
    if (!guard.acquired()) {
        std::cout << "[Sync] Skipped periodic clip sync: lock held by another backend instance\n";
        return;
    }
    sync_clips_all_phases();
}

void SyncService::run_startup_sync() {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    SyncRedisGuard guard(cache_);
    if (!guard.acquired()) {
        std::cout << "[Sync] Skipped startup sync: lock held by another backend instance\n";
        return;
    }
    sync_top_games();
    sync_clips_all_phases();
}

void SyncService::start_periodic(int interval_minutes) {
    interval_minutes_ = interval_minutes;
    std::cout << "[Sync] Periodic sync every " << interval_minutes_ << " min\n";
    schedule_next();
}

void SyncService::schedule_next() {
    timer_.expires_after(std::chrono::minutes(interval_minutes_));
    timer_.async_wait([this](boost::system::error_code ec) {
        if (ec) return;
        sync_now();
        schedule_next();
    });
}

void SyncService::recompute_recommendations_periodic() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_reco_recompute_ < std::chrono::hours(1)) return;
    last_reco_recompute_ = now;
    try {
        auto users = clip_repo_.get_recent_active_user_ids(30, 300);
        for (auto user_id : users) {
            auto similar = clip_repo_.get_similar_broadcasters(user_id, 12);
            if (!similar.empty()) clip_repo_.upsert_user_recommendations(user_id, similar);
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
