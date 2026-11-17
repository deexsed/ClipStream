#include "database/Migrations.hpp"
#include "repositories/SyncRepository.hpp"
#include "services/CacheService.hpp"
#include "services/SyncService.hpp"
#include "utils/Environment.hpp"
#include <chrono>
#include <ctime>
#include <iostream>
#include <stdexcept>

using namespace clipstream;
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        const auto name = utils::env("CLIPSTREAM_TEST_DATABASE");
        if (name.empty() || utils::env("CLIPSTREAM_TEST_REDIS_HOST").empty())
            return 77;
        check(name.ends_with("_test"), "Refusing non-test database");
        database::DbConfig cfg{.host = utils::env("MYSQL_HOST"),
                               .database = name,
                               .user = utils::env("MYSQL_USER"),
                               .password = utils::env("MYSQL_PASSWORD"),
                               .pool_size = 4};
        database::ConnectionPool pool(cfg);
        database::migrate(pool, utils::env("MIGRATIONS_DIR"));
        services::CacheService cache(utils::env("CLIPSTREAM_TEST_REDIS_HOST"), 6379);
        repositories::SyncRepository repo(pool);
        const auto prefix =
            "recovery-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto now = static_cast<int64_t>(std::time(nullptr));
        const auto end = (now / 60 - 1) * 60;
        const auto old = end - 3 * 86400;
        auto sql = [&](const std::string& text) {
            database::ScopedConnection c(pool);
            database::query(c.raw(), text.c_str());
            return mysql_insert_id(c.raw());
        };
        auto scalar = [&](const std::string& text) {
            database::ScopedConnection c(pool);
            database::query(c.raw(), text.c_str());
            database::Result res(mysql_store_result(c.raw()));
            auto row = mysql_fetch_row(res);
            return row && row[0] ? std::stoull(row[0]) : 0ULL;
        };
        auto game = [&](const std::string& suffix, int64_t fresh, bool scheduled = false) {
            const auto id =
                sql("INSERT INTO games(twitch_id,name) VALUES('" + prefix + suffix + "','Recovery fixture')");
            sql("INSERT INTO clip_sync_games(game_id,fresh_until,history_until,scheduled_at) VALUES(" +
                std::to_string(id) + "," + std::to_string(fresh) + "," + std::to_string(end - 40 * 86400) +
                "," + std::to_string(scheduled ? now : 0) + ")");
            return id;
        };
        auto enqueue = [&](uint64_t id, const std::string& suffix, const std::string& phase, int64_t start,
                           int64_t finish, unsigned pages = 0, int64_t retry = 0) {
            const auto window_id = sql("INSERT INTO clip_sync_windows(game_id,phase,start_epoch,end_epoch,"
                                       "`cursor`,pages,raw_count,is_partition,retry_at) VALUES(" +
                                       std::to_string(id) + ",'" + phase + "'," + std::to_string(start) +
                                       "," + std::to_string(finish) + ",'" + (pages ? "saved-cursor" : "") +
                                       "'," + std::to_string(pages) + "," + std::to_string(pages * 100) +
                                       ",1," + std::to_string(retry) + ")");
            return repositories::SyncWindow{
                window_id, id,     prefix + suffix, phase,      pages ? "saved-cursor" : "",
                start,     finish, pages,           pages * 100};
        };
        const auto a = game("-outage", old);
        const auto b = game("-collision", old);
        auto saved = enqueue(a, "-outage", "fresh", old - 900, end, 4, now + 600);
        enqueue(b, "-collision", "fresh", old - 900, end, 5, now + 600);
        enqueue(b, "-collision", "recovery", old - 900, end, 2, now + 700);
        repo.schedule(now, 60);
        const auto own = "game_id IN(" + std::to_string(a) + "," + std::to_string(b) + ")";
        check(scalar("SELECT COUNT(*) FROM clip_sync_windows WHERE " + own +
                     " AND phase='fresh' AND start_epoch>=" + std::to_string(end - 3600)) == 2,
              "Three-day outage did not produce independent live heads");
        check(scalar("SELECT COUNT(*) FROM clip_sync_windows WHERE " + own +
                     " AND phase='fresh' AND start_epoch<" + std::to_string(end - 3600)) == 0,
              "Recovery range remained in the fresh queue");
        check(scalar("SELECT COUNT(*) FROM clip_sync_windows WHERE game_id=" + std::to_string(a) +
                     " AND phase='recovery' AND pages=4 AND raw_count=400 AND `cursor`='saved-cursor'"
                     " AND retry_at=" +
                     std::to_string(now + 600)) == 1,
              "Checkpoint or retry deadline was lost while moving outage work");
        check(scalar("SELECT COUNT(*) FROM clip_sync_windows WHERE game_id=" + std::to_string(b) +
                     " AND phase='recovery' AND pages=5 AND raw_count=500 AND `cursor`='saved-cursor'"
                     " AND retry_at=" +
                     std::to_string(now + 700)) == 1,
              "Duplicate recovery range rewound the checkpoint or bypassed backoff");
        check(scalar("SELECT COUNT(*) FROM clip_sync_windows WHERE game_id=" + std::to_string(a) +
                     " AND phase='recovery' AND start_epoch=" + std::to_string(old - 900) +
                     " AND end_epoch=" + std::to_string(end - 900)) == 1,
              "The missed interval between the previous poll and live head disappeared");
        models::Clip accepted;
        accepted.twitch_clip_id = prefix + "-five";
        accepted.title = "Recovery retains the five-view threshold";
        accepted.view_count = 5;
        auto rejected = accepted;
        rejected.twitch_clip_id = prefix + "-four";
        rejected.view_count = 4;
        services::FetchResult page;
        page.clips = {accepted, rejected};
        page.raw_count = 2;
        repo.commit_page(saved, page, 1, 60, now);
        check(scalar("SELECT COUNT(*) FROM clips WHERE twitch_clip_id='" + accepted.twitch_clip_id + "'") ==
                  0,
              "A stale worker committed the old fresh task after recovery handoff");
        auto recovery = saved;
        recovery.phase = "recovery";
        recovery.id = scalar("SELECT id FROM clip_sync_windows WHERE game_id=" + std::to_string(a) +
                             " AND phase='recovery' AND pages=4");
        repo.commit_page(recovery, page, 1, 60, now);
        check(scalar("SELECT COUNT(*) FROM clips WHERE twitch_clip_id='" + accepted.twitch_clip_id + "'") ==
                      1 &&
                  scalar("SELECT COUNT(*) FROM clips WHERE twitch_clip_id='" + rejected.twitch_clip_id +
                         "'") == 0,
              "Recovery changed the five-view minimum");
        check(scalar("SELECT fresh_feedback_end FROM clip_sync_games WHERE game_id=" + std::to_string(a)) ==
                  0,
              "Old recovery page changed live category feedback");
        auto archive = enqueue(a, "-outage", "history", old - 7200, old - 3600);
        repo.commit_page(archive, page, 1, 60, now);
        check(scalar("SELECT inserted FROM clip_sync_events WHERE game_id=" + std::to_string(a) +
                     " AND phase='history' ORDER BY id DESC LIMIT 1") == 0 &&
                  repositories::SyncRepository::minimum_views("history") == 50,
              "Archive history no longer enforces 50 views");

        // A cheap, endless recovery backlog must not take live dispatch slots.
        for (int i = 0; i < 40; ++i) {
            const auto suffix = "-live-" + std::to_string(i);
            auto id = game(suffix, end, true);
            enqueue(id, suffix, "fresh", end - 900, end);
        }
        for (int i = 0; i < 12; ++i) {
            const auto suffix = "-background-" + std::to_string(i);
            auto id = game(suffix, end, true);
            enqueue(id, suffix, "recovery", old - 900, old);
            if (!i)
                enqueue(id, suffix, "history", old - 7200, old - 3600);
        }
        const auto event_floor = scalar("SELECT COALESCE(MAX(id),0) FROM clip_sync_events");
        services::TwitchService twitch(
            prefix, "secret", &cache, [](const auto& host, const auto&, const auto&, bool, const auto&) {
                services::TwitchService::HttpResult r;
                r.status = 200;
                r.body = host == "id.twitch.tv" ? R"({"access_token":"app","expires_in":3600})"
                                                : R"({"data":[],"pagination":{}})";
                return r;
            });
        repositories::ClipRepository clips(pool);
        boost::asio::io_context io;
        services::SyncService worker(twitch, clips, pool, io, &cache);
        worker.sync_now();
        auto phase_events = [&](const std::string& phase) {
            return scalar("SELECT COUNT(*) FROM clip_sync_events WHERE id>" + std::to_string(event_floor) +
                          " AND phase='" + phase + "'");
        };
        check(phase_events("fresh") >= 24 && phase_events("recovery") >= 1 && phase_events("history") >= 1 &&
                  phase_events("recovery") + phase_events("history") <= 6,
              "Recovery displaced live heads or starved ordinary history");
        auto usage = cache.twitch_rate_status(prefix);
        check(usage["background_budget"] == 120 && usage["background_reserve_percent"] == 25 &&
                  usage["requests_last_minute"] == usage["background_last_minute"],
              "Recovery escaped the shared background budget or user reserve");
        check(cache.acquire_twitch_slot("twitch:rate:" + prefix + ":app", prefix, false),
              "Recovery blocked interactive requests");
        sql("DELETE FROM clips WHERE twitch_clip_id LIKE '" + prefix + "%'");
        sql("DELETE FROM games WHERE twitch_id LIKE '" + prefix + "%'");
        std::cout << "Outage recovery preserves checkpoints, 5/50 thresholds and live/user priority\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
