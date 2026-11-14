#include "database/Migrations.hpp"
#include "repositories/ClipRepository.hpp"
#include "repositories/CommentRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "services/ClipService.hpp"
#include "services/SyncService.hpp"
#include "utils/Environment.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace clipstream;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        const auto name = utils::env("CLIPSTREAM_TEST_DATABASE");
        if (name.empty())
            return 77;
        if (!name.ends_with("_test"))
            throw std::runtime_error("Test database required");
        database::DbConfig cfg{.host = utils::env("MYSQL_HOST", "127.0.0.1"),
                               .database = name,
                               .user = utils::env("MYSQL_USER"),
                               .password = utils::env("MYSQL_PASSWORD"),
                               .pool_size = 3,
                               .read_timeout_seconds = 60,
                               .write_timeout_seconds = 60};
        database::ConnectionPool pool(cfg);
        database::migrate(pool, utils::env("MIGRATIONS_DIR", "database/migrations"));
        database::migrate(pool, utils::env("MIGRATIONS_DIR", "database/migrations"));
        repositories::UserRepository users(pool);
        repositories::ClipRepository clips(pool);
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::vector<uint64_t> user_ids, clip_ids;
        for (int i = 0; i < 5; ++i) {
            models::User u;
            u.username = "rec" + suffix + std::to_string(i);
            u.email = u.username + "@example.test";
            u.password_hash = "!";
            user_ids.push_back(users.create(u));
        }
        for (int i = 0; i < 24; ++i) {
            models::Clip c;
            c.twitch_clip_id = "reco" + suffix + std::to_string(i);
            c.title = "Recommendation test " + std::to_string(i);
            c.duration = 30;
            c.view_count = i < 4 ? 10000 : 0;
            c.broadcaster_id = std::to_string(i);
            c.broadcaster_name = "rec-channel" + std::to_string(i);
            c.twitch_created_at = i < 4 ? "2020-01-01T00:00:00Z" : "2099-01-01T00:00:00Z";
            clip_ids.push_back(clips.create(c));
        }
        auto profile = clips.recommendation_profile(user_ids[0]);
        check(profile.interactions.empty(), "Old data must not manufacture interactions");
        clips.record_user_view(user_ids[0], clip_ids[0]);
        clips.record_user_view(user_ids[0], clip_ids[0]);
        profile = clips.recommendation_profile(user_ids[0]);
        check(profile.interactions.size() == 1, "Repeated opens must contribute once");
        for (size_t u = 0; u < user_ids.size(); ++u) {
            for (int c = 0; c < 3; ++c)
                clips.upsert_user_like(user_ids[u], clip_ids[c], true);
            if (u) {
                clips.upsert_user_like(user_ids[u], clip_ids[4], true);
                clips.upsert_user_like(user_ids[u], clip_ids[5], false);
            }
        }
        clips.refresh_recommendation_features(0, 2000);
        clips.refresh_collaborative_predictions(user_ids[0]);
        auto candidates = clips.recommendation_candidates(user_ids[0], nullptr, {});
        double positive = 0, negative = 0;
        bool old = false, fresh = false;
        for (const auto &c : candidates) {
            if (c.clip.id == clip_ids[4])
                positive = c.collaborative;
            if (c.clip.id == clip_ids[5])
                negative = c.collaborative;
            old |= c.evergreen;
            fresh |= c.discovery;
            if (c.clip.id == clip_ids[0])
                check(c.opens == 1, "Unique open counter inflated");
        }
        check(positive > 0 && negative < 0, "Collaborative predictions must include both vote directions");
        check(old && fresh, "Old and zero-view new clips must remain eligible");
        clips.upsert_user_like(user_ids[0], clip_ids[4], false);
        candidates = clips.recommendation_candidates(user_ids[0], nullptr, {});
        for (const auto &c : candidates)
            check(c.clip.id != clip_ids[4], "Own dislike not applied immediately");
        clips.upsert_user_like(user_ids[0], clip_ids[4], false);
        clips.add_feed_block(user_ids[0], "bc:2");
        repositories::FeedMuteSet mutes;
        clips.load_feed_mutes(user_ids[0], mutes);
        candidates = clips.recommendation_candidates(user_ids[0], &mutes, {});
        for (const auto &c : candidates)
            check(c.clip.broadcaster_id != "2", "Blocked channel leaked");
        clips.record_twitch_snapshot(clip_ids[0], 10000);
        clips.record_twitch_snapshot(clip_ids[0], 10000);
        {
            database::ScopedConnection conn(pool);
            database::query(conn.raw(), ("SELECT COUNT(*) FROM clip_twitch_snapshots WHERE clip_id=" +
                                         std::to_string(clip_ids[0]))
                                            .c_str());
            database::Result res(mysql_store_result(conn.raw()));
            check(std::string(mysql_fetch_row(res)[0]) == "1", "Snapshot bucket must be idempotent");
        }
        if (!utils::env("REDIS_HOST").empty()) {
            services::CacheService cache(utils::env("REDIS_HOST"), 6379);
            // Local recommendation maintenance must not depend on OAuth/Twitch availability.
            std::atomic<unsigned> upstream_calls{0};
            services::TwitchService twitch("", "", nullptr,
                                           [&](const auto &, const auto &, const auto &, bool,
                                               const auto &) -> services::TwitchService::HttpResult {
                                               ++upstream_calls;
                                               throw std::runtime_error("Unexpected upstream call");
                                           });
            boost::asio::io_context ioc;
            {
                database::ScopedConnection conn(pool);
                database::query(conn.raw(),
                                ("UPDATE recommendation_clip_features SET audience_score=-1 WHERE clip_id=" +
                                 std::to_string(clip_ids[0]))
                                    .c_str());
            }
            services::SyncService worker(twitch, clips, pool, ioc, &cache);
            worker.start_periodic(1, false);
            bool features_refreshed = false;
            for (int attempt = 0; attempt < 100 && !features_refreshed; ++attempt) {
                {
                    database::ScopedConnection conn(pool);
                    database::query(
                        conn.raw(),
                        ("SELECT audience_score FROM recommendation_clip_features WHERE clip_id=" +
                         std::to_string(clip_ids[0]))
                            .c_str());
                    database::Result res(mysql_store_result(conn.raw()));
                    if (auto row = mysql_fetch_row(res))
                        features_refreshed = std::stod(row[0]) >= 0;
                }
                if (!features_refreshed)
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            worker.stop();
            check(features_refreshed && upstream_calls == 0, "Feature worker depends on Twitch");
            services::ClipService service(clips, users, cache);
            auto concurrent = std::async(std::launch::async, [&] {
                return service.get_home_feed(user_ids[0], 6, "", suffix + "race", std::nullopt, "rush");
            });
            auto concurrent_page =
                service.get_home_feed(user_ids[0], 6, "", suffix + "race", std::nullopt, "rush");
            check(concurrent.get() == concurrent_page, "Concurrent first-page requests diverged");
            auto first = service.get_home_feed(user_ids[0], 6, "", suffix, std::nullopt, "rush");
            auto repeat = service.get_home_feed(user_ids[0], 6, "", suffix, std::nullopt, "rush");
            check(first == repeat, "Repeated first page changed");
            check(!first["next_cursor"].is_null(), "Missing next cursor");
            const auto token = first["items"][0]["clip"]["delivery_id"].get<std::string>();
            service.record_recommendation_impressions(user_ids[1], {token});
            service.record_recommendation_impressions(user_ids[0], {token, token});
            service.record_recommendation_impressions(user_ids[0], {token});
            bool rejected = false;
            try {
                service.get_home_feed(user_ids[1], 6, first["next_cursor"], suffix, std::nullopt, "rush");
            } catch (const std::invalid_argument &) {
                rejected = true;
            }
            check(rejected, "Cursor leaked across users");
            rejected = false;
            try {
                service.get_home_feed(user_ids[0], 6, first["next_cursor"], suffix, std::nullopt, "home");
            } catch (const std::invalid_argument &) {
                rejected = true;
            }
            check(rejected, "Rush cursor reused in home");
            const auto blocked = first["items"][6 - 1]["clip"]["broadcaster_id"].get<std::string>();
            clips.add_feed_block(user_ids[0], "bc:" + blocked);
            auto refreshed = service.get_home_feed(user_ids[0], 6, "", suffix, std::nullopt, "rush");
            for (const auto &row : refreshed["items"])
                check(row["clip"]["broadcaster_id"] != blocked, "Queue bypassed new block");
            auto next =
                service.get_home_feed(user_ids[0], 6, first["next_cursor"], suffix, std::nullopt, "rush");
            for (const auto &r : next["items"])
                for (const auto &f : first["items"])
                    check(r["clip"]["id"] != f["clip"]["id"], "Duplicate across pages");
            auto other_session =
                service.get_home_feed(user_ids[0], 24, "", suffix + "x", std::nullopt, "rush");
            const auto impressed = first["items"][0]["clip"]["id"];
            for (const auto &r : other_session["items"])
                check(r["clip"]["id"] != impressed, "Rush repeated recent impression");
            auto guest_page = service.get_home_feed(0, 48, "", suffix + "regenerate");
            check(!guest_page["items"].empty(), "Guest fixture missing");
            {
                database::ScopedConnection conn(pool);
                std::string sql = "UPDATE clips SET moderation_hidden=1 WHERE id IN (";
                bool first_id = true;
                for (const auto &r : guest_page["items"]) {
                    if (!first_id)
                        sql += ',';
                    first_id = false;
                    sql += std::to_string(r["clip"]["id"].get<uint64_t>());
                }
                database::query(conn.raw(), (sql + ")").c_str());
            }
            models::Clip replacement;
            replacement.twitch_clip_id = "replacement" + suffix;
            replacement.title = "New catalog arrival";
            replacement.broadcaster_id = "replacement";
            replacement.broadcaster_name = "replacement";
            replacement.duration = 30;
            const auto replacement_id = clips.create(replacement);
            clip_ids.push_back(replacement_id);
            auto regenerated = service.get_home_feed(0, 48, "", suffix + "regenerate");
            check(!regenerated["items"].empty(), "An obsolete queue hid newly arrived clips");
            services::CacheService unavailable("127.0.0.1", 1);
            services::ClipService degraded(clips, users, unavailable);
            auto fallback = degraded.get_home_feed(0, 6, "", suffix + "fallback");
            check(fallback["degraded"] == true && fallback["next_cursor"].is_null() &&
                      !fallback["items"].empty(),
                  "Redis outage prevented the SQL-backed first page");
        }
        for (auto id : clip_ids)
            clips.remove(id);
        for (auto id : user_ids)
            users.remove(id);
        std::cout << "Recommendation database contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
