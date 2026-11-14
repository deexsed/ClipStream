#include "database/Migrations.hpp"
#include "repositories/SyncRepository.hpp"
#include "services/SyncService.hpp"
#include "utils/Environment.hpp"
#include <array>
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
                               .pool_size = 3};
        database::ConnectionPool pool(cfg);
        database::migrate(pool, utils::env("MIGRATIONS_DIR"));
        services::CacheService cache(utils::env("CLIPSTREAM_TEST_REDIS_HOST"), 6379);
        const auto prefix = "sync-test-" + std::to_string(std::time(nullptr));
        // Background exhaustion must preserve interactive capacity and deny without
        // spending it.
        for (int i = 0; i < 120; ++i)
            check(cache.acquire_twitch_slot(prefix + ":bucket", prefix, true), "Background budget shrank");
        check(!cache.acquire_twitch_slot(prefix + ":bucket", prefix, true), "Background limit exceeded");
        check(cache.acquire_twitch_slot(prefix + ":bucket", prefix, false),
              "Importer starved interactive request");
        auto rate = cache.twitch_rate_status(prefix);
        check(rate["requests_last_minute"] == 121 && rate["background_last_minute"] == 120,
              "Denied request spent quota");
        cache.update_rate_limit(prefix + ":headers", 200, static_cast<int>(std::time(nullptr) + 60), 800);
        check(!cache.acquire_twitch_slot(prefix + ":headers", prefix + "-headers", true),
              "Background breached header reserve");
        check(cache.acquire_twitch_slot(prefix + ":headers", prefix + "-headers", false),
              "Header reserve blocked interactive request");
        const auto now = (std::time(nullptr) / 60) * 60;
        uint64_t game;
        {
            database::ScopedConnection c(pool);
            database::query(
                c.raw(),
                ("INSERT INTO games(twitch_id,name) VALUES('" + prefix + "','Sync fixture')").c_str());
            game = mysql_insert_id(c.raw());
        }
        repositories::SyncRepository repo(pool);
        // Existing fixtures from other suites must not affect this test's
        // queue/counters.
        auto own_window = [&]() -> std::optional<repositories::SyncWindow> {
            database::ScopedConnection c(pool);
            database::query(c.raw(),
                            ("SELECT id,phase,start_epoch,end_epoch,`cursor`,pages,raw_count "
                             "FROM "
                             "clip_sync_windows WHERE game_id=" +
                             std::to_string(game) + " AND phase='fresh' ORDER BY end_epoch DESC,id LIMIT 1")
                                .c_str());
            database::Result res(mysql_store_result(c.raw()));
            auto r = mysql_fetch_row(res);
            if (!r)
                return std::nullopt;
            return repositories::SyncWindow{std::stoull(r[0]),
                                            game,
                                            prefix,
                                            r[1],
                                            r[4],
                                            std::stoll(r[2]),
                                            std::stoll(r[3]),
                                            static_cast<unsigned>(std::stoul(r[5])),
                                            static_cast<unsigned>(std::stoul(r[6]))};
        };
        auto counts = [&]() {
            database::ScopedConnection c(pool);
            database::query(c.raw(), ("SELECT COALESCE(SUM(inserted),0),COALESCE(SUM(updated),0),"
                                      "COALESCE(SUM(unchanged),0) FROM clip_sync_events WHERE game_id=" +
                                      std::to_string(game))
                                         .c_str());
            database::Result res(mysql_store_result(c.raw()));
            auto r = mysql_fetch_row(res);
            return std::array<uint64_t, 3>{std::stoull(r[0]), std::stoull(r[1]), std::stoull(r[2])};
        };
        for (int i = 0; i < 100 && !own_window(); ++i)
            repo.schedule(now, 60);
        auto w = own_window();
        check(w && w->end <= now - 60, "Fresh window includes unindexed future");
        services::FetchResult result;
        check(repositories::SyncRepository::minimum_views("fresh") == 5 &&
                  repositories::SyncRepository::minimum_views("history") == 50,
              "Import thresholds changed");
        models::Clip clip;
        clip.twitch_clip_id = prefix;
        clip.title = "Sync fixture";
        clip.view_count = 5;
        clip.twitch_created_at = "2026-11-14T12:00:00Z";
        result.clips.push_back(clip);
        auto low = clip;
        low.twitch_clip_id = prefix + "-four";
        low.view_count = 4;
        result.clips.push_back(low);
        low.twitch_clip_id = prefix + "-zero";
        low.view_count = 0;
        result.clips.push_back(low);
        result.raw_count = 3;
        result.cursor = "next";
        const auto unstarted = *w;
        repo.schedule(now + 60, 60);
        w = own_window();
        check(w && w->id == unstarted.id && w->end > unstarted.end, "Unstarted polls were not coalesced");
        repo.commit_page(unstarted, result, 2);
        check(counts()[0] == 0, "Old worker committed a superseded time window");
        const auto original = *w;
        repo.commit_page(*w, result, 2);
        check(counts()[0] == 1, "Write boundary admitted clips below five views");
        // Stale replay must not import or increment counters a second time.
        repo.commit_page(original, result, 2);
        w = own_window();
        check(w && w->pages == 1 && w->cursor == "next", "Page checkpoint missing");
        auto bad = result;
        bad.raw_count = 4;
        bad.clips[0].title = "Must be rolled back";
        bad.clips.push_back(clip);
        bad.clips.back().twitch_clip_id = prefix + "-bad";
        bad.clips.back().title.assign(10000, 'x');
        bool rolled_back = false;
        try {
            repo.commit_page(*w, bad, 1);
        } catch (const database::Error&) {
            rolled_back = true;
        }
        check(rolled_back && own_window()->pages == 1, "Failed page advanced checkpoint");
        auto after_failure = repo.dashboard();
        check(counts()[1] == 0, "Failed page committed counters");
        result.clips[0].view_count = 50;
        result.cursor = "next-2";
        repo.commit_page(*w, result, 1);
        w = own_window();
        result.cursor.clear();
        repo.commit_page(*w, result, 1);
        auto data = repo.dashboard();
        check(data["hourly"].size() == 24, "Hourly series has gaps");
        auto observed = counts();
        check(observed == std::array<uint64_t, 3>{1, 1, 1}, "Insert/update/no-op counts incorrect");
        check(!own_window(), "Completed fresh window retained");
        // New scheduling is independent of historical cursors, with a lookback for
        // clips gaining views.
        repo.schedule(now + 120, 60);
        check(repo.next("fresh", false, now + 120).has_value() && repo.next("history").has_value(),
              "History blocked fresh schedule");
        w = own_window();
        result.clips.clear();
        result.raw_count = 900;
        result.cursor = "over-cap";
        repo.commit_page(*w, result, 1);
        auto split = own_window();
        check(split && split->end - split->start < w->end - w->start, "Saturated window was not split");
        // A request failure preserves its cursor, adds backoff and records real
        // upstream 429 separately.
        repo.fail(*split, services::TwitchError(429, "upstream limited", 60, 429), 1);
        data = repo.dashboard();
        for (const auto& p : data["phases"])
            if (p["phase"] == "fresh")
                check(p["twitch_429"] >= 1 && p["deferred"] >= 1, "429 accounting missing");
        // Real Helix decoding: retain pagination even when filtering an entire
        // page.
        unsigned requests = 0;
        services::TwitchService twitch(
            prefix, "secret", nullptr,
            [&](const auto& host, const auto& path, const auto&, bool, const auto&) {
                services::TwitchService::HttpResult r;
                r.status = 200;
                if (host == "id.twitch.tv")
                    r.body = R"({"access_token":"app","expires_in":3600})";
                else if (path.starts_with("/helix/clips"))
                    r.body = R"({"data":[{"id":"low","view_count":4}],"pagination":{"cursor":"more"}})";
                else
                    r.body = R"({"data":[]})";
                return r;
            });
        {
            services::TwitchService::BackgroundScope background(requests);
            auto filtered = twitch.fetch_clips("1", 100, "", "", "", 5, true);
            check(filtered.clips.empty() && filtered.raw_count == 1 && filtered.cursor == "more" &&
                      filtered.below_threshold,
                  "Five-view minimum or filtered page cursor broken");
        }
        check(requests >= 2, "Enrichment/HTTP background accounting missing");
        // Cleanup this test's fixture; other contract suites have their own users
        // and clips.
        {
            database::ScopedConnection c(pool);
            database::query(c.raw(), ("DELETE FROM clips WHERE twitch_clip_id='" + prefix + "'").c_str());
            database::query(c.raw(), ("DELETE FROM games WHERE id=" + std::to_string(game)).c_str());
        }
        std::cout << "Sync scheduling, exact counters, split windows and "
                     "background reserve passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
