#include "controllers/AuthController.hpp"
#include "database/Migrations.hpp"
#include "repositories/AccountSecurityRepository.hpp"
#include "repositories/ClipRepository.hpp"
#include "repositories/CommentRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "services/AuthService.hpp"
#include "services/CacheService.hpp"
#include "services/ClipService.hpp"
#include "utils/Environment.hpp"
#include "utils/Errors.hpp"
#include <atomic>
#include <algorithm>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace clipstream;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        const auto name = utils::env("CLIPSTREAM_TEST_DATABASE");
        if (name.empty()) {
            std::cout << "CLIPSTREAM_TEST_DATABASE is not set; integration skipped\n";
            return 77;
        }
        if (!name.ends_with("_test"))
            throw std::runtime_error("Integration database name must end in _test");
        database::DbConfig cfg{.host = utils::env("MYSQL_HOST", "127.0.0.1"),
                               .port = static_cast<uint16_t>(std::stoul(utils::env("MYSQL_PORT", "3306"))),
                               .database = name,
                               .user = utils::env("MYSQL_USER", "clipstream"),
                               .password = utils::env("MYSQL_PASSWORD"),
                               .pool_size = 1,
                               .acquire_timeout = std::chrono::milliseconds(150)};
        // DDL/GET_LOCK needs a separate deadline from short interactive API queries.
        auto migration_cfg = cfg;
        migration_cfg.read_timeout_seconds = 10;
        migration_cfg.write_timeout_seconds = 10;
        {
            database::ConnectionPool migration_pool(migration_cfg);
            database::ScopedConnection connection(migration_pool);
            database::query(connection.raw(), "SELECT SLEEP(6)");
            database::Result completed(mysql_store_result(connection.raw()));
            require(completed && mysql_fetch_row(completed), "Migration query exceeded API deadline");
        }
        database::ConnectionPool pool(cfg);
        database::migrate(pool, utils::env("MIGRATIONS_DIR", "database/migrations"));
        database::migrate(pool, utils::env("MIGRATIONS_DIR", "database/migrations"));
        require(database::schema_ready(pool), "Schema not ready");
        {
            database::ScopedConnection held(pool);
            auto waiter = std::async(std::launch::async, [&] {
                try {
                    database::ScopedConnection second(pool);
                    return false;
                } catch (const database::Error &) {
                    return true;
                }
            });
            require(waiter.get(), "Pool deadline missing");
        }
        repositories::UserRepository users(pool);
        repositories::ClipRepository clips(pool);
        repositories::CommentRepository comments(pool);
        services::AuthService auth(users, std::string(64, 'x'), 24);
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        auto a = auth.login_twitch("900" + suffix, "a" + suffix, "a" + suffix + "@example.test", "");
        auto b = auth.login_twitch("901" + suffix, "b" + suffix, "b" + suffix + "@example.test", "");
        require(a && b, "OAuth user creation failed");
        auth.save_twitch_oauth(a->user.id, "fixture-a-" + suffix, "fixture-refresh", "2099-01-01T00:00:00Z");
        auth.save_twitch_oauth(b->user.id, "fixture-b-" + suffix, "fixture-refresh", "2099-01-01T00:00:00Z");
        models::User local;
        local.username = "local-a" + suffix;
        local.email = "local-a" + suffix + "@example.test";
        local.password_hash = "!twitch-only";
        auto local_a = users.create(local);
        local.username = "local-b" + suffix;
        local.email = "local-b" + suffix + "@example.test";
        auto local_b = users.create(local);
        require(local_a && local_b, "NULL Twitch IDs must not collide");
        require(!auth.verify_token(auth.renew_token(local_a)), "Local password-era session accepted");
        {
            database::ScopedConnection connection(pool);
            const auto legacy = "UPDATE users SET chat_banned_until='2038-01-01 00:00:00' WHERE id=" +
                                std::to_string(local_a);
            database::query(connection.raw(), legacy.c_str());
            // Simulate interruption after the ADD succeeded but before migration007 was recorded.
            database::query(connection.raw(), "DELETE FROM schema_migrations WHERE version=7");
        }
        database::migrate(pool, utils::env("MIGRATIONS_DIR", "database/migrations"));
        require(users.find_by_id(local_a)->chat_banned_permanently &&
                    users.find_by_id(local_a)->chat_banned_until.empty(),
                "Legacy permanent ban was not migrated on interrupted retry");
        users.remove(local_b);
        users.remove(local_a);
        require(auth.verify_token(a->token).has_value(), "Valid JWT rejected");
        uint64_t game_id = 0;
        {
            database::ScopedConnection connection(pool);
            const auto sql =
                "INSERT INTO games(twitch_id,name) VALUES('fixture-" + suffix + "','SQL contract')";
            database::query(connection.raw(), sql.c_str());
            game_id = mysql_insert_id(connection.raw());
        }
        models::Clip first;
        first.game_id = game_id;
        first.broadcaster_id = a->user.twitch_id;
        first.broadcaster_name = a->user.username;
        first.creator_twitch_id = b->user.twitch_id;
        first.twitch_clip_id = "test-" + suffix;
        first.title = "Regression fixture";
        first.thumbnail_url = "https://example.test/thumb";
        first.embed_url = "https://example.test/embed";
        first.duration = 30;
        first.classification_source = "manual";
        first.view_count = 200;
        first.author_id = a->user.id;
        first.twitch_created_at = "2026-11-14T00:00:00Z";
        auto clip = clips.create(first);
        first.twitch_clip_id += "-second";
        first.classification_source = "unknown";
        first.is_mature = true;
        first.author_id = b->user.id;
        auto second = clips.create(first);
        require(clips.find_by_id(clip).has_value(), "Single-connection find_by_id deadlocked");
        repositories::ClipFilter filter;
        filter.period = repositories::TimePeriod::All;
        require(users.save_user_settings(a->user.id, {"unmatched-keyword"}, {"ru"}), "Settings save failed");
        auto settings = users.get_user_settings(a->user.id);
        require(!settings.contains("hide_mature") && settings["mute_keywords"][0] == "unmatched-keyword" &&
                    settings["clip_languages"][0] == "ru", "Retired filter leaked or settings shifted");
        {
            database::ScopedConnection connection(pool);
            database::query(connection.raw(), ("UPDATE user_settings SET hide_mature=1 WHERE user_id=" +
                            std::to_string(a->user.id)).c_str());
        }
        repositories::FeedMuteSet retired_filter;
        require(clips.load_feed_mutes(a->user.id, retired_filter), "User mutes failed");
        const auto unclassified = clips.find_filtered(filter, 20, 0, &retired_filter);
        require(std::any_of(unclassified.begin(), unclassified.end(), [&](const auto &item) {return item.id == second;}),
                "Legacy strict flag hid an unclassified clip after sign-in");
        {
            database::ScopedConnection connection(pool);
            database::query(connection.raw(), "DELETE FROM schema_migrations WHERE version=8");
        }
        database::migrate(pool, utils::env("MIGRATIONS_DIR", "database/migrations"));
        {
            database::ScopedConnection connection(pool);
            database::query(connection.raw(), ("SELECT hide_mature FROM user_settings WHERE user_id=" + std::to_string(a->user.id)).c_str());
            database::Result result(mysql_store_result(connection.raw()));
            auto row = mysql_fetch_row(result);
            require(row && std::string(row[0]) == "0", "Retirement migration did not reset legacy settings");
        }
        require(users.save_user_settings(a->user.id, {}, {}), "Settings reset failed");
        require(!clips.find_filtered(filter, 20, 0).empty(), "Feed empty or tags nested acquire");
        require(clips.popular_feed_position(clip, repositories::TimePeriod::All, nullptr, 50).has_value(),
                "Trend ranks SQL invalid");
        models::Comment comment;
        comment.clip_id = clip;
        comment.user_id = b->user.id;
        comment.body = "root";
        auto parent = comments.create(comment);
        comment.clip_id = second;
        comment.parent_id = parent;
        bool rejected = false;
        try {
            comments.create(comment);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(rejected, "Cross-clip reply accepted");
        {
            database::ScopedConnection conn(pool);
            for (int i = 0; i < 12; ++i) {
                auto sql = "INSERT INTO comments(clip_id,user_id,parent_id,body) VALUES(" +
                           std::to_string(clip) + "," + std::to_string(b->user.id) + "," +
                           std::to_string(parent) + ",'legacy deep')";
                database::query(conn.raw(), sql.c_str());
                parent = mysql_insert_id(conn.raw());
            }
        }
        auto tree = comments.find_by_clip(clip, 30, 0, "recent", b->user.id);
        require(!tree.empty(), "Deep comments deadlocked");
        auto node = &tree.front();
        while (!node->replies.empty())
            node = &node->replies.front();
        require(node->replies_truncated, "Deep comment truncation not exposed");
        require(!comments.find_by_clip(clip, 30, 0, "recent", b->user.id, node->id).empty(),
                "Deep comment continuation unavailable");
        clips.upsert_user_like(b->user.id, clip, true);
        require(clips.find_by_id(clip)->like_count == 1, "Vote count not updated");
        clips.upsert_user_like(b->user.id, clip, true);
        require(clips.find_by_id(clip)->like_count == 0, "Vote removal count wrong");
        clips.record_analytics_engagement(b->user.id, "session-" + suffix, clip, false, 10);
        clips.record_analytics_engagement(b->user.id, "session-" + suffix, clip, false, 15);
        clips.record_analytics_engagement(b->user.id, "session-" + suffix, clip, true, 20);
        clips.record_analytics_engagement(b->user.id, "session-" + suffix, clip, true, 30);
        {
            database::ScopedConnection conn(pool);
            auto sql = "SELECT total_watch_sec,clips_viewed FROM analytics_sessions WHERE user_id=" +
                       std::to_string(b->user.id);
            database::query(conn.raw(), sql.c_str());
            database::Result result(mysql_store_result(conn.raw()));
            auto row = mysql_fetch_row(result);
            require(row && std::string(row[0]) == "20" && std::string(row[1]) == "1",
                    "Analytics delta/final idempotence wrong");
        }
        repositories::AccountSecurityRepository security(pool);
        {
            database::ScopedConnection conn(pool);
            database::query(conn.raw(),
                            "UPDATE admin_bootstrap SET "
                            "eligible=1,twitch_id=NULL,completed_by=NULL,completed_at=NULL WHERE id=1");
        }
        require(!security.bootstrap_admin(b->user.id, a->user.twitch_id),
                "Wrong Twitch identity bootstrapped admin");
        require(security.bootstrap_admin(a->user.id, a->user.twitch_id), "Canonical Twitch bootstrap failed");
        require(!security.bootstrap_admin(b->user.id, b->user.twitch_id), "Bootstrap was not singleton");
        require(auth.get_user(a->user.id)->role == models::UserRole::Admin, "Bootstrap role missing");
        security.restrict_user(a->user.id, b->user.id, "chat_ban", 0, "regression permanent chat ban");
        const auto permanent = security.user_state(a->user.id, b->user.id);
        require(permanent["chat_banned_permanently"] == true && permanent["chat_banned_until"].is_null(),
                "Permanent ban has an expiry");
        require(!users.may_post_comment(b->user.id), "Permanent chat ban allowed posting");
        require(!users.create_wall_post(b->user.id, b->user.id, "must be rejected"),
                "Permanent chat ban allowed wall posting");
        models::Comment banned_comment;
        banned_comment.clip_id = clip;
        banned_comment.user_id = b->user.id;
        banned_comment.body = "must be rejected";
        bool chat_rejected = false;
        try {
            comments.create(banned_comment);
        } catch (const utils::PermissionDenied &) {
            chat_rejected = true;
        }
        require(chat_rejected, "Atomic comment path bypassed permanent chat ban");
        security.restrict_user(a->user.id, b->user.id, "chat_ban", 1, "regression temporary replacement");
        require(!users.find_by_id(b->user.id)->chat_banned_permanently && !users.may_post_comment(b->user.id),
                "Temporary replacement did not clear permanent flag");
        security.restrict_user(a->user.id, b->user.id, "lift_chat", 0, "regression lift");
        require(users.may_post_comment(b->user.id) && !users.find_by_id(b->user.id)->chat_banned_permanently,
                "Lifting chat ban did not restore posting");
        auto old_b = auth.renew_token(b->user.id);
        security.restrict_user(a->user.id, b->user.id, "block_account", 0, "regression permanent ban");
        require(!auth.verify_token(old_b), "Permanent ban failed to revoke JWT");
        security.restrict_user(a->user.id, b->user.id, "unblock_account", 0, "regression unblock");
        require(!auth.verify_token(old_b), "Unblock restored revoked JWT");
        old_b = auth.renew_token(b->user.id);
        security.restrict_user(a->user.id, b->user.id, "suspend_account", 1, "regression suspension");
        require(!auth.verify_token(old_b), "Suspension failed to revoke JWT");
        security.restrict_user(a->user.id, b->user.id, "unblock_account", 0, "regression unblock");
        require(!auth.verify_token(old_b), "Unblock revived revoked JWT");
        bool denied = false;
        try {
            security.restrict_user(a->user.id, a->user.id, "block_account", 0, "self");
        } catch (const std::invalid_argument &) {
            denied = true;
        }
        require(denied, "Self lockout allowed");
        denied = false;
        try {
            users.update_role(a->user.id, models::UserRole::User, a->user.id);
        } catch (const std::invalid_argument &) {
            denied = true;
        }
        require(denied, "Last admin demotion allowed");
        auto ban = security.ban_ip(a->user.id, "::ffff:192.0.2.7", 1, "fixture IP", "192.0.2.8");
        require(security.is_ip_banned("192.0.2.7"), "Mapped IPv6 IP ban bypass");
        security.unban_ip(a->user.id, ban);
        require(!security.is_ip_banned("192.0.2.7"), "IP unban failed");
        services::CacheService cache("127.0.0.1", 6379);
        services::TwitchService twitch("fixture", "fixture", &cache);
        controllers::AuthController auth_controller(auth, twitch, cache, "http://localhost/auth/callback",
                                                    security);
        server::Router auth_router;
        auth_controller.register_routes(auth_router);
        require(auth_router.dispatch(server::http::verb::post, "/auth/login", "{}", "").status == 410,
                "Password login still exposed");
        require(auth_router.dispatch(server::http::verb::post, "/auth/register", "{}", "").status == 410,
                "Password registration still exposed");
        {
            auto concurrent_cfg = cfg;
            concurrent_cfg.pool_size = 4;
            concurrent_cfg.acquire_timeout = std::chrono::milliseconds(2000);
            database::ConnectionPool concurrent_pool(concurrent_cfg);
            repositories::ClipRepository concurrent_clips(concurrent_pool);
            repositories::CommentRepository concurrent_comments(concurrent_pool);
            auto task1 = std::async(std::launch::async, [&] {
                for (int i = 0; i < 8; ++i)
                    require(concurrent_clips.upsert_user_like(b->user.id, clip, true).ok,
                            "Reciprocal vote failed");
            });
            auto task2 = std::async(std::launch::async, [&] {
                for (int i = 0; i < 8; ++i)
                    require(concurrent_clips.upsert_user_like(a->user.id, second, true).ok,
                            "Reciprocal vote failed");
            });
            task1.get();
            task2.get();
            require(clips.find_by_id(clip)->like_count == 0 && clips.find_by_id(second)->like_count == 0,
                    "Concurrent vote counts diverged");
            models::Comment root;
            root.clip_id = clip;
            root.user_id = b->user.id;
            root.body = "Concurrent removal";
            auto baseline = users.find_by_id(a->user.id)->reputation;
            auto remove_id = comments.create(root);
            auto d1 = std::async(std::launch::async, [&] {
                return concurrent_comments.remove_authorized(remove_id, b->user.id);
            });
            auto d2 = std::async(std::launch::async, [&] {
                return concurrent_comments.remove_authorized(remove_id, b->user.id);
            });
            require(static_cast<int>(d1.get()) + static_cast<int>(d2.get()) == 1,
                    "Duplicate removal succeeded twice");
            require(users.find_by_id(a->user.id)->reputation == baseline,
                    "Duplicate comment removal changed reputation twice");
            root.user_id = a->user.id;
            root.body = "Notification parent";
            auto notification_parent = comments.create(root);
            root.user_id = b->user.id;
            root.parent_id = notification_parent;
            root.body = std::string(119, 'a') + "😀";
            auto reply = comments.create(root);
            require(comments.find_by_id(reply).has_value(),
                    "Unicode notification caused partial write error");
        }
        auto visible = *clips.find_by_id(clip);
        security.set_clip_visibility(a->user.id, clip, true, "fixture hide");
        require(!clips.find_by_id(clip), "Hidden clip leaked by ID");
        visible.view_count = 777;
        clips.create(visible);
        require(!clips.find_by_id(clip), "Twitch sync restored a moderator-hidden clip");
        security.set_clip_visibility(a->user.id, clip, false, "fixture restore");
        require(clips.find_by_id(clip)->view_count == 777, "Restored clip metadata stale");
        clips.insert_content_report(b->user.id, "clip", clip, "Fixture report");
        auto reports = clips.list_content_reports(10, 0);
        require(!reports.empty(), "Report missing");
        security.resolve_report(a->user.id, reports.front()["id"].get<uint64_t>(), "actioned",
                                "fixture resolved");
        {
            auto before = clips.find_by_id(clip)->view_count;
            services::ClipService service(clips, users, cache);
            service.record_view(clip, b->user.id);
            service.record_view(clip, 0);
            require(clips.find_by_id(clip)->view_count == before, "Site opens changed Twitch view_count");
        }
        if (const char *redis_host = std::getenv("CLIPSTREAM_TEST_REDIS_HOST")) {
            services::CacheService shared_cache(redis_host, 6379);
            {
                database::ScopedConnection connection(pool);
                database::query(connection.raw(), "INSERT IGNORE INTO tags(name) VALUES('cache:contract')");
                const auto sql = "INSERT IGNORE INTO clip_tags(clip_id,tag_id) SELECT " +
                                 std::to_string(clip) + ",id FROM tags WHERE name='cache:contract'";
                database::query(connection.raw(), sql.c_str());
            }
            services::ClipService cached_service(clips, users, shared_cache);
            {
                database::ScopedConnection connection(pool);
                database::query(connection.raw(), "SELECT @@SESSION.sql_mode");
                database::Result modes(mysql_store_result(connection.raw()));
                auto mode = mysql_fetch_row(modes);
                require(mode && mode[0] &&
                            std::string(mode[0]).find("ONLY_FULL_GROUP_BY") != std::string::npos,
                        "Feed regressions require strict MySQL GROUP BY mode");
            }
            auto check_path = [&](const char *name, auto operation) {
                try {
                    operation();
                } catch (const std::exception &error) {
                    throw std::runtime_error(std::string(name) + ": " + error.what());
                }
            };
            for (const auto viewer : {uint64_t{0}, b->user.id}) {
                for (const auto sort : {repositories::ClipSort::Popular, repositories::ClipSort::Recent,
                                        repositories::ClipSort::Duration}) {
                    for (const auto period :
                         {repositories::TimePeriod::Today, repositories::TimePeriod::Week,
                          repositories::TimePeriod::Month, repositories::TimePeriod::All}) {
                        repositories::ClipFilter f;
                        f.sort = sort;
                        f.period = period;
                        check_path("feed", [&] { cached_service.get_feed(f, 20, 0, viewer); });
                        check_path("personalized_feed", [&] {
                            cached_service.get_personalized_feed(f, {a->user.twitch_id}, 20, 0, viewer);
                        });
                    }
                }
                check_path("home_sections", [&] {
                    cached_service.get_home_sections(viewer, 6, {a->user.twitch_id}, "strict-sections");
                });
                check_path("home_feed", [&] {
                    auto home = cached_service.get_home_feed(viewer, 24, {}, "strict-home");
                    require(home.contains("items"), "Home payload contract changed");
                    if (!viewer)
                        require(!home["items"].empty(), "Anonymous home omitted public fixture");
                });
                check_path("home_language_filter", [&] {
                    cached_service.get_home_feed(viewer, 24, {}, "", std::vector<std::string>{"ru"});
                });
                check_path("related",
                           [&] { cached_service.get_related_clips(clip, viewer, 12, std::nullopt, 42, {}); });
                check_path("trend_ranks", [&] { cached_service.get_clip_trend_ranks(clip, viewer); });
                check_path("search", [&] { cached_service.search("Regression fixture", 20, 0, viewer); });
                check_path("discussed",
                           [&] { cached_service.get_discussed_clips(24, 12, viewer, std::nullopt); });
                check_path("continue_watching",
                           [&] { cached_service.get_continue_watching(12, viewer, std::nullopt); });
                check_path("channel_updates",
                           [&] { cached_service.get_channel_updates_for_viewer(12, viewer, std::nullopt); });
                check_path("random", [&] { cached_service.get_random_clip(viewer, std::nullopt); });
                for (const auto scope : {"all", "channel", "created"})
                    check_path("entity_clips", [&] {
                        cached_service.get_entity_clips(a->user.id, a->user.twitch_id, scope, 12, 0, viewer);
                    });
            }
            check_path("recommended_preferences", [&] {
                clips.find_recommended(12, {}, {game_id}, {a->user.twitch_id}, {b->user.twitch_id},
                                       repositories::RecommendationWeights{}, b->user.id);
            });
            check_path("similar_streamers", [&] { cached_service.get_similar_streamers(b->user.id, 12); });
            check_path("following_feed", [&] { cached_service.get_following_feed(12, 0, b->user.id); });
            check_path("friends_feed", [&] { cached_service.get_friends_feed(12, 0, b->user.id); });

            repositories::ClipFilter tagged;
            tagged.period = repositories::TimePeriod::All;
            tagged.tag = "cache:contract";
            require(!cached_service.get_feed(tagged, 20, 0).empty(), "Tagged fixture missing");
            tagged.tag = "cache";
            tagged.broadcaster_name = "contract:";
            require(cached_service.get_feed(tagged, 20, 0).empty(),
                    "Delimited feed cache identity collision");
            services::TwitchService fake_twitch("fixture", "fixture", nullptr,
                                                [](const auto &, const auto &, const auto &, bool,
                                                   const auto &) -> services::TwitchService::HttpResult {
                                                    throw std::runtime_error(
                                                        "Fresh OAuth session unexpectedly called Twitch");
                                                });
            services::AuthService first_auth(users, std::string(64, 'x'), 24),
                second_auth(users, std::string(64, 'x'), 24);
            first_auth.bind_twitch(fake_twitch, shared_cache);
            second_auth.bind_twitch(fake_twitch, shared_cache);
            users.mark_oauth_validated(b->user.id);
            auto token = users.find_by_id(b->user.id)->twitch_access_token;
            auto lock = "oauth:refresh:" + std::to_string(b->user.id);
            shared_cache.set_required(lock, "other-replica", 90);
            auto f1 =
                std::async(std::launch::async, [&] { return first_auth.user_access_token(b->user.id); });
            auto f2 =
                std::async(std::launch::async, [&] { return second_auth.user_access_token(b->user.id); });
            require(f1.get() == token && f2.get() == token,
                    "Fresh OAuth unnecessarily required a shared refresh lease");
            shared_cache.compare_delete(lock, "other-replica");
        }
        clips.remove(second);
        clips.remove(clip);
        {
            database::ScopedConnection connection(pool);
            const auto sql = "DELETE FROM games WHERE id=" + std::to_string(game_id);
            database::query(connection.raw(), sql.c_str());
        }
        users.remove(b->user.id);
        users.remove(a->user.id);
        std::cout << "Database contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
