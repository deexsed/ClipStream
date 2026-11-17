#include "repositories/SyncRepository.hpp"
#include "database/SqlEscape.hpp"
#include "repositories/ClipRepository.hpp"
#include "repositories/RecommendationWrites.hpp"
#include <algorithm>
#include <ctime>

namespace clipstream::repositories {
namespace {
void exec(MYSQL* c, const std::string& sql) { database::query(c, sql.c_str()); }
void add_window(MYSQL* c, uint64_t game, const std::string& phase, int64_t start, int64_t end,
                bool partition = false) {
    if (start >= end)
        return;
    exec(c, "INSERT IGNORE INTO "
            "clip_sync_windows(game_id,phase,start_epoch,end_epoch,`cursor`,is_partition) "
            "VALUES(" +
                std::to_string(game) + ",'" + database::escape(c, phase) + "'," + std::to_string(start) +
                "," + std::to_string(end) + ",''," + (partition ? "1" : "0") + ")");
}
uint64_t num(MYSQL_ROW row, int i) { return row[i] ? std::stoull(row[i]) : 0; }
} // namespace

unsigned SyncRepository::fresh_poll_delay(unsigned base_seconds, unsigned empty_streak) {
    base_seconds = std::clamp(base_seconds, 60u, 86400u);
    const auto multiplier = 1u << std::min(empty_streak, 5u);
    return std::max(base_seconds, std::min(1800u, base_seconds * multiplier));
}

void SyncRepository::schedule(int64_t now, int interval_seconds) {
    // Helix truncates timestamps to minutes. Wait for indexing; recheck 15
    // minutes for growing views.
    const auto end = (now / 60 - 1) * 60;
    database::ScopedConnection conn(pool_);
    auto c = conn.raw();
    database::Transaction tx(c);
    exec(c, "INSERT IGNORE INTO "
            "clip_sync_games(game_id,fresh_until,history_until) SELECT id," +
                std::to_string(end - 900) + "," + std::to_string(end - 900) + " FROM games");
    // A long outage is recovery work, not live polling. Lock games before windows,
    // in the same order as page commits, and preserve every durable checkpoint.
    const auto live_floor = end - 3600;
    const auto expired = "phase='fresh' AND start_epoch<" + std::to_string(live_floor);
    exec(c, "SELECT s.game_id FROM clip_sync_games s WHERE EXISTS "
            "(SELECT 1 FROM clip_sync_windows w WHERE w.game_id=s.game_id AND w." +
                expired + ") ORDER BY s.game_id FOR UPDATE");
    {
        database::Result locked_games(mysql_store_result(c));
    }
    exec(c,
         "INSERT INTO clip_sync_windows(game_id,phase,start_epoch,end_epoch,`cursor`,pages,raw_count,"
         "is_partition,retry_at) SELECT f.game_id,'recovery',f.start_epoch,f.end_epoch,f.`cursor`,f.pages,"
         "f.raw_count,f.is_partition,f.retry_at FROM clip_sync_windows f WHERE f.phase='fresh' AND "
         "f.start_epoch<" +
             std::to_string(live_floor) +
             " ON DUPLICATE KEY UPDATE "
             "`cursor`=IF(VALUES(pages)>clip_sync_windows.pages,VALUES(`cursor`),clip_sync_windows.`cursor`),"
             "raw_count=IF(VALUES(pages)>clip_sync_windows.pages,VALUES(raw_count),clip_sync_windows.raw_"
             "count),"
             "is_partition=GREATEST(clip_sync_windows.is_partition,VALUES(is_partition)),"
             "retry_at=GREATEST(clip_sync_windows.retry_at,VALUES(retry_at)),"
             "pages=GREATEST(clip_sync_windows.pages,VALUES(pages))");
    exec(c, "DELETE FROM clip_sync_windows WHERE " + expired);
    struct Game {
        uint64_t id;
        int64_t fresh, history;
    };
    std::vector<Game> games;
    const auto eligible = "fresh_next_poll_at<=" + std::to_string(now) +
                          " AND scheduled_at<=" + std::to_string(now - interval_seconds);
    auto select_games = [&](bool active, size_t limit, bool exploration = false) {
        std::string exclude;
        for (const auto& game : games)
            exclude += (exclude.empty() ? "" : ",") + std::to_string(game.id);
        exec(c, "SELECT game_id,fresh_until,history_until FROM clip_sync_games WHERE " + eligible +
                    (active        ? " AND fresh_productive_end>0 AND fresh_empty_streak=0"
                     : exploration ? " AND (fresh_productive_end=0 OR fresh_empty_streak>0)"
                                   : "") +
                    (exclude.empty() ? "" : " AND game_id NOT IN(" + exclude + ")") +
                    " ORDER BY fresh_next_poll_at,scheduled_at,game_id LIMIT " + std::to_string(limit) +
                    " FOR UPDATE");
        database::Result res(mysql_store_result(c));
        while (auto row = mysql_fetch_row(res))
            games.push_back({num(row, 0), std::stoll(row[1]), std::stoll(row[2])});
    };
    // Leave exploration slots even when productive categories remain continuously due.
    select_games(true, 8);
    select_games(false, 4, true);
    if (games.size() < 12)
        select_games(false, 12 - games.size());
    for (const auto& g : games) {
        if (g.fresh < end) {
            auto start = g.fresh - 900;
            if (start < live_floor) {
                const auto live_start = end - 900;
                add_window(c, g.id, "recovery", start, live_start);
                start = live_start;
            }
            // Merge unstarted polls; preserve smaller partitions created to escape the Helix cap.
            exec(c, "SELECT id FROM clip_sync_windows WHERE game_id=" + std::to_string(g.id) +
                        " AND phase='fresh' AND pages=0 AND is_partition=0 ORDER BY id LIMIT 1 FOR UPDATE");
            database::Result head(mysql_store_result(c));
            auto existing = mysql_fetch_row(head);
            const auto head_id = existing ? num(existing, 0) : 0;
            head.reset();
            if (head_id)
                exec(c, "UPDATE clip_sync_windows SET end_epoch=GREATEST(end_epoch," + std::to_string(end) +
                            ") WHERE id=" + std::to_string(head_id));
            else
                add_window(c, g.id, "fresh", start, end);
        }
        exec(c, "UPDATE clip_sync_games SET fresh_until=GREATEST(fresh_until," + std::to_string(end) +
                    "),scheduled_at=" + std::to_string(now) + " WHERE game_id=" + std::to_string(g.id));
        exec(c, "SELECT 1 FROM clip_sync_windows WHERE game_id=" + std::to_string(g.id) +
                    " AND phase='history' LIMIT 1");
        database::Result pending(mysql_store_result(c));
        const bool exists = mysql_fetch_row(pending) != nullptr;
        pending.reset();
        const auto floor = end - 30 * 86400;
        if (!exists && g.history > floor) {
            const auto start = std::max(floor, g.history - 3600);
            add_window(c, g.id, "history", start, g.history);
            exec(c, "UPDATE clip_sync_games SET history_until=" + std::to_string(start) +
                        " WHERE game_id=" + std::to_string(g.id));
        }
    }
    exec(c, "DELETE FROM clip_sync_events WHERE "
            "created_at<UTC_TIMESTAMP()-INTERVAL 30 DAY LIMIT 1000");
    tx.commit();
}

bool SyncRepository::fresh_catching_up(int64_t now) {
    if (!now)
        now = std::time(nullptr);
    database::ScopedConnection conn(pool_);
    exec(conn.raw(), "SELECT 1 FROM clip_sync_windows w JOIN clip_sync_games s ON s.game_id=w.game_id "
                     "WHERE w.phase='fresh' AND w.end_epoch<=" +
                         std::to_string(now - 300) + " AND w.retry_at<=" + std::to_string(now) +
                         " AND (w.pages>0 OR w.is_partition=1 OR s.fresh_next_poll_at<=" +
                         std::to_string(now) + ") LIMIT 1");
    database::Result res(mysql_store_result(conn.raw()));
    return mysql_fetch_row(res) != nullptr;
}

std::optional<SyncWindow> SyncRepository::next(const std::string& phase, bool oldest, int64_t now,
                                               const std::vector<uint64_t>& excluded) {
    if (!now)
        now = std::time(nullptr);
    database::ScopedConnection conn(pool_);
    std::string exclude;
    for (auto id : excluded)
        exclude += (exclude.empty() ? "" : ",") + std::to_string(id);
    exec(conn.raw(), "SELECT "
                     "w.id,w.game_id,g.twitch_id,w.phase,w.start_epoch,w.end_epoch,w.`cursor`,"
                     "w.pages,w.raw_count FROM clip_sync_windows w JOIN games g ON "
                     "g.id=w.game_id JOIN clip_sync_games s ON s.game_id=w.game_id WHERE w.phase='" +
                         database::escape(conn.raw(), phase) + "' AND w.retry_at<=" + std::to_string(now) +
                         (phase == "fresh" ? " AND (w.pages>0 OR w.is_partition=1 OR s.fresh_next_poll_at<=" +
                                                 std::to_string(now) + ")"
                                           : "") +
                         (exclude.empty() ? "" : " AND w.id NOT IN(" + exclude + ")") +
                         " ORDER BY w.end_epoch " + (oldest ? "ASC" : "DESC") + ",w.pages,w.id LIMIT 1");
    database::Result res(mysql_store_result(conn.raw()));
    auto row = mysql_fetch_row(res);
    if (!row)
        return std::nullopt;
    return SyncWindow{num(row, 0),
                      num(row, 1),
                      row[2],
                      row[3],
                      row[6],
                      std::stoll(row[4]),
                      std::stoll(row[5]),
                      static_cast<unsigned>(num(row, 7)),
                      static_cast<unsigned>(num(row, 8))};
}

void SyncRepository::commit_page(const SyncWindow& w, const services::FetchResult& result, unsigned requests,
                                 unsigned base_interval_seconds, int64_t now) {
    if (!now)
        now = std::time(nullptr);
    if (result.raw_count < result.clips.size())
        throw std::invalid_argument("Clip page accepted count exceeds raw count");
    database::ScopedConnection conn(pool_);
    auto c = conn.raw();
    database::Transaction tx(c);
    // Same lock order as scheduling: category before windows.
    exec(c, "SELECT fresh_empty_streak,fresh_feedback_end,fresh_productive_end FROM clip_sync_games "
            "WHERE game_id=" +
                std::to_string(w.game_id) + " FOR UPDATE");
    database::Result category(mysql_store_result(c));
    auto game = mysql_fetch_row(category);
    if (!game)
        return;
    const auto empty_streak = static_cast<unsigned>(num(game, 0));
    const auto feedback_end = std::stoll(game[1]);
    const auto productive_end = std::stoll(game[2]);
    category.reset();
    // A stale worker cannot commit a page after another owner advanced/deleted
    // the task.
    exec(c, "SELECT pages,start_epoch,end_epoch FROM clip_sync_windows WHERE id=" + std::to_string(w.id) +
                " FOR UPDATE");
    database::Result lock(mysql_store_result(c));
    auto row = mysql_fetch_row(lock);
    if (!row || num(row, 0) != w.pages || std::stoll(row[1]) != w.start || std::stoll(row[2]) != w.end)
        return;
    lock.reset();
    unsigned inserted = 0, updated = 0, unchanged = 0;
    for (auto clip : result.clips) {
        // Enforce the write contract independently of caller-side filtering.
        if (clip.view_count < minimum_views(w.phase) || clip.twitch_clip_id.empty())
            continue;
        clip.game_id = w.game_id;
        if (clip.embed_url.empty())
            clip.embed_url = "https://clips.twitch.tv/embed?clip=" + clip.twitch_clip_id;
        uint64_t affected = 0;
        auto id = ClipRepository::create_on_connection(c, clip, affected);
        if (affected == 1)
            ++inserted;
        else if (affected == 2)
            ++updated;
        else
            ++unchanged;
        exec(c, "INSERT IGNORE INTO "
                "clip_twitch_snapshots(clip_id,observed_at,view_count) VALUES(" +
                    std::to_string(id) + ",FROM_UNIXTIME(FLOOR(UNIX_TIMESTAMP()/1800)*1800)," +
                    std::to_string(clip.view_count) + ")");
        if (affected || mysql_affected_rows(c) > 0)
            mark_recommendation_dirty(c, 0, id);
    }
    const bool saturated = !result.cursor.empty() && !result.below_threshold &&
                           (w.raw_count + result.raw_count >= 900 || w.pages + 1 >= 9);
    const auto middle = ((w.start + w.end) / 120) * 60;
    const bool split = saturated && middle > w.start && middle < w.end;
    std::string status = "ok";
    if (split) {
        add_window(c, w.game_id, w.phase, w.start, middle, true);
        add_window(c, w.game_id, w.phase, middle, w.end, true);
        status = "split";
    } else if (saturated)
        status = "saturated";
    const bool finished = saturated || result.cursor.empty() || result.below_threshold;
    const auto qualified = inserted + updated + unchanged;
    // A late empty backlog page cannot override recent productive feedback. The
    // productive watermark also remembers accepted clips from earlier pages/splits.
    if (w.phase == "fresh" && ((qualified && w.end >= feedback_end && w.end > productive_end) ||
                               (!split && finished && w.end > feedback_end))) {
        const auto streak = qualified || productive_end >= w.end ? 0u : std::min(empty_streak, 30u) + 1;
        const auto delay = fresh_poll_delay(base_interval_seconds, streak);
        exec(c, "UPDATE clip_sync_games SET fresh_empty_streak=" + std::to_string(streak) +
                    ",fresh_next_poll_at=" + std::to_string(now + delay) +
                    ",fresh_feedback_end=" + std::to_string(w.end) +
                    (qualified ? ",fresh_productive_end=GREATEST(fresh_productive_end," +
                                     std::to_string(w.end) + ")"
                               : "") +
                    " WHERE game_id=" + std::to_string(w.game_id));
    }
    if (split || finished) {
        exec(c, "DELETE FROM clip_sync_windows WHERE id=" + std::to_string(w.id));
    } else {
        exec(c, "UPDATE clip_sync_windows SET `cursor`='" + database::escape(c, result.cursor) +
                    "',pages=pages+1,"
                    "raw_count=raw_count+" +
                    std::to_string(result.raw_count) + " WHERE id=" + std::to_string(w.id));
    }
    exec(c, "INSERT INTO "
            "clip_sync_events(phase,status,game_id,fetched,inserted,updated,"
            "unchanged,filtered,requests) VALUES('" +
                database::escape(c, w.phase) + "','" + status + "'," + std::to_string(w.game_id) + "," +
                std::to_string(result.raw_count) + "," + std::to_string(inserted) + "," +
                std::to_string(updated) + "," + std::to_string(unchanged) + "," +
                std::to_string(result.raw_count - inserted - updated - unchanged) + "," +
                std::to_string(requests) + ")");
    tx.commit();
}

void SyncRepository::fail(const SyncWindow& w, const services::TwitchError& e, unsigned requests) {
    database::ScopedConnection conn(pool_);
    auto c = conn.raw();
    database::Transaction tx(c);
    const bool reset = e.upstream_status == 400 || e.upstream_status == 404;
    exec(c, "UPDATE clip_sync_windows SET retry_at=UNIX_TIMESTAMP()+" +
                std::to_string(e.upstream_status == 404
                                   ? 3600
                                   : std::clamp(e.retry_after ? e.retry_after : 60, 5, 3600)) +
                // Keep the checkpoint generation monotonic, even when discarding an invalid cursor.
                (reset ? ",`cursor`='',pages=pages+1,raw_count=0" : "") +
                " WHERE id=" + std::to_string(w.id) + " AND pages=" + std::to_string(w.pages) +
                " AND start_epoch=" + std::to_string(w.start) + " AND end_epoch=" + std::to_string(w.end));
    exec(c, "INSERT INTO "
            "clip_sync_events(phase,status,game_id,requests,upstream_status) "
            "VALUES('" +
                database::escape(c, w.phase) + "','" +
                (e.status == 429 || e.status == 503 ? "deferred" : "error") + "'," +
                std::to_string(w.game_id) + "," + std::to_string(requests) + "," +
                std::to_string(e.upstream_status) + ")");
    tx.commit();
}

void SyncRepository::record_discovery(const std::string& status, unsigned requests, int upstream_status) {
    database::ScopedConnection conn(pool_);
    exec(conn.raw(), "INSERT INTO clip_sync_events(phase,status,requests,upstream_status) "
                     "VALUES('categories','" +
                         database::escape(conn.raw(), status) + "'," + std::to_string(requests) + "," +
                         std::to_string(upstream_status) + ")");
}

nlohmann::json SyncRepository::dashboard() {
    using json = nlohmann::json;
    database::ScopedConnection conn(pool_);
    auto c = conn.raw();
    json out = {{"hourly", json::array()}, {"phases", json::array()}, {"categories", json::array()}};
    exec(c, "SELECT COUNT(*),SUM(moderation_hidden=0 AND unavailable_at IS "
            "NULL),COUNT(DISTINCT game_id),"
            "SUM(created_at>=UTC_TIMESTAMP()-INTERVAL 24 HOUR),"
            "SUM(twitch_created_ts>=UTC_TIMESTAMP()-INTERVAL 4 HOUR),"
            "DATE_FORMAT(MAX(created_at),'%Y-%m-%dT%H:%i:%sZ'),(SELECT COUNT(*) "
            "FROM games) FROM clips");
    database::Result res(mysql_store_result(c));
    if (auto r = mysql_fetch_row(res))
        out["catalog"] = {{"total", num(r, 0)},
                          {"visible", num(r, 1)},
                          {"categories_with_clips", num(r, 2)},
                          {"added_24h", num(r, 3)},
                          {"created_on_twitch_4h", num(r, 4)},
                          {"last_import_at", r[5] ? json(r[5]) : json(nullptr)},
                          {"categories_known", num(r, 6)}};
    res.reset();
    // A dense, UTC-aligned series; includes imports before the new counters were
    // installed.
    exec(c, "WITH RECURSIVE hours AS (SELECT "
            "TIMESTAMP(DATE_FORMAT(UTC_TIMESTAMP()-INTERVAL 23 HOUR,"
            "'%Y-%m-%d %H:00:00')) AS hour UNION ALL SELECT hour+INTERVAL 1 HOUR "
            "FROM hours WHERE "
            "hour<TIMESTAMP(DATE_FORMAT(UTC_TIMESTAMP(),'%Y-%m-%d %H:00:00'))) "
            "SELECT DATE_FORMAT(h.hour,'%Y-%m-%dT%H:%i:%sZ'),COUNT(c.id) FROM "
            "hours h LEFT JOIN clips c "
            "ON c.created_at>=h.hour AND c.created_at<h.hour+INTERVAL 1 HOUR "
            "GROUP BY h.hour ORDER BY h.hour");
    res = database::Result(mysql_store_result(c));
    while (auto r = mysql_fetch_row(res))
        out["hourly"].push_back({{"hour", r[0]}, {"inserted", num(r, 1)}});
    res.reset();
    exec(c, "SELECT "
            "phase,SUM(fetched),SUM(inserted),SUM(updated),SUM(unchanged),SUM("
            "filtered),SUM(requests),"
            "SUM(status='deferred'),SUM(status='error'),SUM(upstream_status=429),SUM("
            "status='saturated'),"
            "DATE_FORMAT(MAX(created_at),'%Y-%m-%dT%H:%i:%sZ') FROM clip_sync_events "
            "WHERE created_at>=UTC_TIMESTAMP()-INTERVAL 24 HOUR GROUP BY phase");
    res = database::Result(mysql_store_result(c));
    while (auto r = mysql_fetch_row(res))
        out["phases"].push_back({{"phase", r[0]},
                                 {"fetched", num(r, 1)},
                                 {"inserted", num(r, 2)},
                                 {"updated", num(r, 3)},
                                 {"unchanged", num(r, 4)},
                                 {"filtered", num(r, 5)},
                                 {"requests", num(r, 6)},
                                 {"deferred", num(r, 7)},
                                 {"errors", num(r, 8)},
                                 {"twitch_429", num(r, 9)},
                                 {"saturated", num(r, 10)},
                                 {"last_activity_at", r[11]}});
    res.reset();
    exec(c, "SELECT phase,COUNT(*),FROM_UNIXTIME(MIN(start_epoch)) FROM "
            "clip_sync_windows GROUP BY phase");
    res = database::Result(mysql_store_result(c));
    out["queue"] = json::array();
    while (auto r = mysql_fetch_row(res))
        out["queue"].push_back({{"phase", r[0]}, {"pending", num(r, 1)}, {"oldest_window_utc", r[2]}});
    res.reset();
    exec(c, "SELECT g.name,COUNT(*) FROM clips c JOIN games g ON g.id=c.game_id "
            "GROUP BY g.id,g.name "
            "ORDER BY COUNT(*) DESC,g.id LIMIT 10");
    res = database::Result(mysql_store_result(c));
    while (auto r = mysql_fetch_row(res))
        out["categories"].push_back({{"name", r[0]}, {"clips", num(r, 1)}});
    return out;
}
} // namespace clipstream::repositories
