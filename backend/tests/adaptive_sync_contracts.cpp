#include "database/Migrations.hpp"
#include "repositories/SyncRepository.hpp"
#include "services/SyncService.hpp"
#include "utils/Environment.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <ctime>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>

using namespace clipstream;
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

int main() {
    try {
        const auto name = utils::env("CLIPSTREAM_TEST_DATABASE");
        const auto host = utils::env("CLIPSTREAM_TEST_REDIS_HOST");
        if (name.empty() || host.empty())
            return 77;
        check(name.ends_with("_test"), "Refusing non-test database");
        database::DbConfig cfg{.host = utils::env("MYSQL_HOST"),
                               .database = name,
                               .user = utils::env("MYSQL_USER"),
                               .password = utils::env("MYSQL_PASSWORD"),
                               .pool_size = 3};
        database::ConnectionPool pool(cfg);
        database::migrate(pool, utils::env("MIGRATIONS_DIR"));
        services::CacheService cache(host, 6379);
        repositories::SyncRepository repo(pool);
        const auto prefix =
            "adaptive-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        auto sql = [&](const std::string& text) {
            database::ScopedConnection c(pool);
            database::query(c.raw(), text.c_str());
        };
        auto create_game = [&](const std::string& suffix, int64_t end) {
            database::ScopedConnection c(pool);
            database::Transaction tx(c.raw());
            database::query(c.raw(), ("INSERT INTO games(twitch_id,name) VALUES('" + prefix + suffix +
                                      "','Adaptive fixture')")
                                         .c_str());
            const auto id = mysql_insert_id(c.raw());
            database::query(c.raw(),
                            ("INSERT INTO clip_sync_games(game_id,fresh_until,history_until) VALUES(" +
                             std::to_string(id) + "," + std::to_string(end) + "," + std::to_string(end) + ")")
                                .c_str());
            tx.commit();
            return id;
        };
        auto clock = (static_cast<int64_t>(std::time(nullptr)) / 60) * 60 + 172800;
        const auto game = create_game("-poll", clock - 60);
        auto enqueue = [&](uint64_t game_id, const std::string& twitch_id, int64_t start, int64_t end,
                           const std::string& phase = "fresh", bool partition = false) {
            database::ScopedConnection c(pool);
            const auto text =
                "INSERT INTO clip_sync_windows(game_id,phase,start_epoch,end_epoch,`cursor`,is_partition) "
                "VALUES(" +
                std::to_string(game_id) + ",'" + phase + "'," + std::to_string(start) + "," +
                std::to_string(end) + ",''," + (partition ? "1" : "0") + ")";
            database::query(c.raw(), text.c_str());
            return repositories::SyncWindow{
                mysql_insert_id(c.raw()), game_id, twitch_id, phase, "", start, end, 0, 0};
        };
        auto feedback = [&]() {
            database::ScopedConnection c(pool);
            database::query(c.raw(), ("SELECT fresh_empty_streak,fresh_next_poll_at,fresh_feedback_end,"
                                      "fresh_productive_end FROM clip_sync_games WHERE game_id=" +
                                      std::to_string(game))
                                         .c_str());
            database::Result res(mysql_store_result(c.raw()));
            auto r = mysql_fetch_row(res);
            return std::array<int64_t, 4>{std::stoll(r[0]), std::stoll(r[1]), std::stoll(r[2]),
                                          std::stoll(r[3])};
        };
        check(repositories::SyncRepository::fresh_poll_delay(60, 0) == 60 &&
                  repositories::SyncRepository::fresh_poll_delay(60, 5) == 1800 &&
                  repositories::SyncRepository::fresh_poll_delay(7200, 30) == 7200,
              "Adaptive interval broke the configured minimum or cap");
        services::FetchResult empty;
        auto window = enqueue(game, prefix + "-poll", clock - 120, clock - 60);
        const std::array<int, 6> delays{120, 240, 480, 960, 1800, 1800};
        for (size_t i = 0; i < delays.size(); ++i) {
            repo.commit_page(window, empty, 1, 60, clock);
            auto state = feedback();
            check(state[0] == static_cast<int64_t>(i + 1) && state[1] == clock + delays[i],
                  "Empty category did not back off or survive repository recreation");
            window = enqueue(game, prefix + "-poll", window.end, clock + 60);
            repositories::SyncRepository resumed(pool);
            auto early = resumed.next("fresh", false, clock + 1);
            check(!early || early->game_id != game, "Queued empty category bypassed cooldown");
            auto due = resumed.next("fresh", false, state[1]);
            check(due && due->id == window.id, "Inactive category was permanently starved");
            clock = state[1];
        }
        services::FetchResult productive;
        models::Clip clip;
        clip.twitch_clip_id = prefix + "-five";
        clip.title = "Productive adaptive fixture";
        clip.view_count = 5;
        productive.clips.push_back(clip);
        productive.raw_count = 1;
        productive.cursor = "more";
        repo.commit_page(window, productive, 3, 60, clock);
        auto active = feedback();
        check(active[0] == 0 && active[1] == clock + 60, "Productive category did not regain normal cadence");
        auto continuation = repo.next("fresh", false, clock + 1);
        check(continuation && continuation->id == window.id && continuation->pages == 1,
              "Cooldown blocked active pagination");
        repo.commit_page(*continuation, empty, 1, 60, clock + 1);
        check(feedback() == active, "Empty last page erased productivity from earlier pages");
        auto older = enqueue(game, prefix + "-poll", window.start - 120, window.start - 60, "fresh", true);
        repo.commit_page(older, empty, 1, 60, clock + 2);
        check(feedback() == active, "Old backlog completion overrode new category feedback");
        auto failed = enqueue(game, prefix + "-poll", window.end, window.end + 600);
        repo.fail(failed, services::TwitchError(502, "fixture failure", 60), 1);
        check(feedback() == active, "Transport failure was treated as an empty category");
        const auto actual_now = static_cast<int64_t>(std::time(nullptr));
        auto cooled = enqueue(game, prefix + "-poll", actual_now - 2400, actual_now - 1800);
        check(!repo.fresh_catching_up(actual_now), "Intentional cooldown falsely triggered catch-up");
        sql("UPDATE clip_sync_windows SET pages=1 WHERE id=" + std::to_string(cooled.id));
        check(repo.fresh_catching_up(actual_now), "Cooldown hid overdue in-progress pagination");
        sql("DELETE FROM clips WHERE twitch_clip_id='" + clip.twitch_clip_id + "'");
        sql("DELETE FROM games WHERE id=" + std::to_string(game));

        // History has its own shared sub-budget; exhausting it cannot block fresh/user requests.
        const auto history_client = prefix + "-history";
        for (int i = 0; i < 24; ++i)
            check(cache.acquire_twitch_slot(history_client, history_client, true, true),
                  "History budget shrank");
        services::TwitchRateDeferral deferral;
        check(!cache.acquire_twitch_slot(history_client, history_client, true, true, &deferral) &&
                  deferral.history_only && deferral.retry_after >= 1 && deferral.retry_after <= 60,
              "History cap or precise retry time missing");
        check(cache.acquire_twitch_slot(history_client, history_client, true),
              "History starved fresh requests");
        check(cache.acquire_twitch_slot(history_client, history_client, false),
              "History starved user requests");
        auto usage = cache.twitch_rate_status(history_client);
        check(usage["history_last_minute"] == 24 && usage["background_last_minute"] == 25 &&
                  usage["requests_last_minute"] == 26,
              "Rejected history request spent shared quota");
        services::TwitchService scoped(
            history_client, "secret", &cache, [](const auto& h, const auto&, const auto&, bool, const auto&) {
                services::TwitchService::HttpResult r;
                r.status = 200;
                r.body = h == "id.twitch.tv" ? R"({"access_token":"app","expires_in":3600})"
                                             : R"({"data":[],"pagination":{}})";
                return r;
            });
        unsigned scoped_requests = 0;
        bool history_paused = false;
        {
            services::TwitchService::BackgroundScope history_scope(scoped_requests, true);
            try {
                scoped.fetch_clips("1", 100, "", "", "", 50, true);
            } catch (const services::TwitchError& e) {
                history_paused = e.history_budget && e.upstream_status == 0;
            }
        }
        check(history_paused && scoped_requests == 0, "History scope missed nested OAuth/metadata admission");
        {
            services::TwitchService::BackgroundScope fresh_scope(scoped_requests);
            scoped.fetch_clips("1", 100, "", "", "", 5, true);
        }
        check(scoped_requests >= 2, "History-only deferral leaked into the fresh scope");

        // End-to-end scheduler regression: ready work must not stop after the old five-page batch.
        const auto real_now = static_cast<int64_t>(std::time(nullptr));
        std::vector<uint64_t> batch_games;
        for (int i = 0; i < 40; ++i) {
            auto suffix = "-batch-" + std::to_string(i);
            auto id = create_game(suffix, real_now - 60);
            batch_games.push_back(id);
            sql("UPDATE clip_sync_games SET fresh_next_poll_at=" +
                std::to_string(real_now - (i < 20 ? 10000 : 1000)) +
                (i < 20 ? ",fresh_productive_end=" + std::to_string(real_now - 3600) : "") +
                " WHERE game_id=" + std::to_string(id));
            const auto fresh_end = real_now - (i < 20 ? 1800 : 60);
            enqueue(id, prefix + suffix, fresh_end - 900, fresh_end);
            enqueue(id, prefix + suffix, real_now - 4500, real_now - 900, "history");
        }
        repo.schedule(real_now, 60);
        {
            database::ScopedConnection c(pool);
            database::query(c.raw(), ("SELECT COUNT(*) FROM clip_sync_games s JOIN games g ON g.id=s.game_id "
                                      "WHERE g.twitch_id LIKE '" +
                                      prefix + "-batch-%' AND s.scheduled_at>0 AND s.fresh_productive_end=0")
                                         .c_str());
            database::Result res(mysql_store_result(c.raw()));
            check(std::stoull(mysql_fetch_row(res)[0]) >= 4,
                  "Productive categories starved exploration slots");
        }
        check(repo.fresh_catching_up(real_now), "Old eligible windows did not trigger catch-up");
        auto first = repo.next("fresh", true, real_now);
        check(first.has_value(), "Overdue queue unexpectedly empty");
        auto second = repo.next("fresh", true, real_now, {first->id});
        check(second && first->id != second->id, "Concurrent fetch selected the same window twice");
        std::atomic<int> clip_requests{0}, in_flight{0}, peak_in_flight{0};
        std::atomic<bool> duplicate{false};
        std::mutex targets_mutex;
        std::unordered_set<std::string> targets;
        services::TwitchService twitch(
            prefix + "-batch", "secret", &cache,
            [&](const auto& h, const auto& target, const auto&, bool, const auto&) {
                services::TwitchService::HttpResult r;
                r.status = 200;
                if (h == "id.twitch.tv")
                    r.body = R"({"access_token":"app","expires_in":3600})";
                else {
                    if (target.starts_with("/helix/clips")) {
                        ++clip_requests;
                        const auto entered = ++in_flight;
                        auto peak = peak_in_flight.load();
                        while (peak < entered && !peak_in_flight.compare_exchange_weak(peak, entered)) {
                        }
                        {
                            std::lock_guard lock(targets_mutex);
                            if (!targets.insert(target).second)
                                duplicate = true;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                        --in_flight;
                    }
                    r.body = R"({"data":[],"pagination":{}})";
                }
                return r;
            });
        repositories::ClipRepository clips(pool);
        boost::asio::io_context io;
        services::SyncService worker(twitch, clips, pool, io, &cache);
        worker.sync_now();
        check(clip_requests >= 25 && clip_requests <= 30,
              "Ready backlog still limited by five-page micro-batches");
        check(peak_in_flight == 2 && !duplicate,
              "Fetch concurrency was unbounded, serial or duplicated a task");
        {
            database::ScopedConnection c(pool);
            database::query(
                c.raw(),
                ("SELECT COUNT(*) FROM clip_sync_windows w JOIN games g ON g.id=w.game_id "
                 "WHERE g.twitch_id LIKE '" +
                 prefix + "-batch-%' AND w.phase='fresh' AND w.end_epoch<=" + std::to_string(real_now - 300))
                    .c_str());
            database::Result res(mysql_store_result(c.raw()));
            check(std::stoull(mysql_fetch_row(res)[0]) <= 2, "New heads starved overdue fresh windows");
        }
        {
            database::ScopedConnection c(pool);
            database::query(c.raw(),
                            ("SELECT COUNT(*) FROM clip_sync_events e JOIN games g ON g.id=e.game_id "
                             "WHERE g.twitch_id LIKE '" +
                             prefix + "-batch-%' AND e.phase='history'")
                                .c_str());
            database::Result res(mysql_store_result(c.raw()));
            check(std::stoull(mysql_fetch_row(res)[0]) <= 3, "History displaced the fresh priority share");
        }
        for (auto id : batch_games)
            sql("DELETE FROM games WHERE id=" + std::to_string(id));
        check(!repo.fresh_catching_up(real_now), "Catch-up stayed active after overdue work drained");
        // Parallel requests must not overspend the shared background budget.
        const auto burst_client = prefix + "-burst";
        for (int i = 0; i < 112; ++i)
            check(cache.acquire_twitch_slot(burst_client, burst_client, true), "Budget preload failed");
        std::vector<std::future<bool>> burst;
        for (int i = 0; i < 32; ++i)
            burst.push_back(std::async(std::launch::async, [&] {
                return cache.acquire_twitch_slot(burst_client, burst_client, true);
            }));
        int admitted = 0;
        for (auto& request : burst)
            if (request.get())
                ++admitted;
        check(admitted == 8, "Parallel admissions exceeded the 120-request background budget");
        check(cache.acquire_twitch_slot(burst_client, burst_client, false),
              "Catch-up exhausted the user reserve");
        std::cout
            << "Adaptive polling, durable cooldowns, backlog fairness and shared history reserve passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
