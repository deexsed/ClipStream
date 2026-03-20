#include <iostream>
#include <cstdlib>
#include <string>
#include <thread>
#include <sstream>
#include <vector>
#include <boost/asio.hpp>

#include "database/ConnectionPool.hpp"
#include "server/HttpServer.hpp"
#include "server/Router.hpp"

#include "repositories/UserRepository.hpp"
#include "repositories/ClipRepository.hpp"
#include "repositories/CommentRepository.hpp"

#include "services/AuthService.hpp"
#include "services/CacheService.hpp"
#include "services/ClipService.hpp"
#include "services/TwitchService.hpp"
#include "services/SyncService.hpp"

#include "controllers/AuthController.hpp"
#include "controllers/ClipController.hpp"
#include "controllers/CommentController.hpp"
#include "controllers/EventSubController.hpp"
#include "controllers/StaffController.hpp"
#include "controllers/AdminController.hpp"
#include "controllers/ModerationController.hpp"
#include "controllers/SocialController.hpp"

#include "middleware/AuthMiddleware.hpp"

using namespace clipstream;

static std::string env(const char* name, const std::string& fallback = "") {
    const char* val = std::getenv(name);
    return val ? std::string(val) : fallback;
}

/// Интервал полного синка клипов с Twitch (минуты). Учитывайте лимиты Helix (bucket, заголовки Ratelimit-*).
static int clip_sync_interval_minutes() {
    const char* raw = std::getenv("CLIP_SYNC_INTERVAL_MINUTES");
    if (!raw || !*raw) return 5;
    try {
        int v = std::stoi(raw);
        if (v < 1) return 1;
        if (v > 120) return 120;
        return v;
    } catch (...) {
        return 5;
    }
}

static bool column_exists(MYSQL* c, const std::string& table, const std::string& column, const std::string& schema) {
    std::string sql =
        "SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA='"
        + schema + "' AND TABLE_NAME='" + table + "' AND COLUMN_NAME='" + column + "' LIMIT 1";
    if (mysql_query(c, sql.c_str()) != 0) return false;
    MYSQL_RES* res = mysql_store_result(c);
    if (!res) return false;
    bool exists = mysql_fetch_row(res) != nullptr;
    mysql_free_result(res);
    return exists;
}

static bool index_exists(MYSQL* c, const std::string& table, const std::string& index_name, const std::string& schema) {
    std::string sql = "SELECT 1 FROM information_schema.STATISTICS WHERE TABLE_SCHEMA='" + schema
        + "' AND TABLE_NAME='" + table + "' AND INDEX_NAME='" + index_name + "' LIMIT 1";
    if (mysql_query(c, sql.c_str()) != 0) return false;
    MYSQL_RES* res = mysql_store_result(c);
    if (!res) return false;
    bool exists = mysql_fetch_row(res) != nullptr;
    mysql_free_result(res);
    return exists;
}

static std::vector<std::string> split_csv(const std::string& csv) {
    std::vector<std::string> out;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

int main() {
    try {
        database::DbConfig db_cfg{
            .host     = env("MYSQL_HOST", "127.0.0.1"),
            .port     = static_cast<uint16_t>(std::stoi(env("MYSQL_PORT", "3306"))),
            .database = env("MYSQL_DATABASE", "clipstream"),
            .user     = env("MYSQL_USER", "clipstream"),
            .password = env("MYSQL_PASSWORD", "changeme_secret"),
            .pool_size = 10
        };

        std::string redis_host   = env("REDIS_HOST", "127.0.0.1");
        uint16_t redis_port      = static_cast<uint16_t>(std::stoi(env("REDIS_PORT", "6379")));
        std::string jwt_secret   = env("JWT_SECRET", "default_jwt_secret_change_me_32ch");
        int jwt_exp              = std::stoi(env("JWT_EXPIRATION_HOURS", "24"));
        std::string redirect_uri = env("TWITCH_REDIRECT_URI", "http://localhost/auth/callback");
        uint16_t server_port     = 8080;

        database::ConnectionPool db_pool(db_cfg);

        // Schema migrations (best-effort, idempotent)
        {
            database::ScopedConnection conn(db_pool);
            const auto& schema = db_cfg.database;
            if (!column_exists(conn.raw(), "clips", "vod_id", schema))
                mysql_query(conn.raw(), "ALTER TABLE clips ADD COLUMN vod_id VARCHAR(64) DEFAULT NULL");
            if (!column_exists(conn.raw(), "clips", "vod_offset", schema))
                mysql_query(conn.raw(), "ALTER TABLE clips ADD COLUMN vod_offset INT DEFAULT 0");
            if (!column_exists(conn.raw(), "clips", "twitch_created_at", schema))
                mysql_query(conn.raw(), "ALTER TABLE clips ADD COLUMN twitch_created_at VARCHAR(64) DEFAULT NULL");
            if (!column_exists(conn.raw(), "clips", "broadcaster_avatar", schema))
                mysql_query(conn.raw(), "ALTER TABLE clips ADD COLUMN broadcaster_avatar VARCHAR(255) DEFAULT ''");
            if (!column_exists(conn.raw(), "clips", "broadcaster_id", schema))
                mysql_query(conn.raw(), "ALTER TABLE clips ADD COLUMN broadcaster_id VARCHAR(64) DEFAULT ''");
            if (!column_exists(conn.raw(), "users", "twitch_access_token", schema))
                mysql_query(conn.raw(), "ALTER TABLE users ADD COLUMN twitch_access_token TEXT NULL");
            if (!column_exists(conn.raw(), "users", "twitch_refresh_token", schema))
                mysql_query(conn.raw(), "ALTER TABLE users ADD COLUMN twitch_refresh_token TEXT NULL");
            if (!column_exists(conn.raw(), "users", "twitch_token_expires_at", schema))
                mysql_query(conn.raw(), "ALTER TABLE users ADD COLUMN twitch_token_expires_at VARCHAR(64) NULL");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS user_recommendations ("
                "user_id BIGINT UNSIGNED PRIMARY KEY,"
                "recommended_broadcasters_json JSON NULL,"
                "calculated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "CONSTRAINT fk_user_reco_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
                ")");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS app_settings ("
                "setting_key VARCHAR(128) PRIMARY KEY,"
                "setting_value TEXT NOT NULL,"
                "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
                ")");
            if (!column_exists(conn.raw(), "clips", "title_fingerprint", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE clips ADD COLUMN title_fingerprint VARCHAR(16) NOT NULL DEFAULT '' AFTER title");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS user_clip_engagement ("
                "user_id BIGINT UNSIGNED NOT NULL,"
                "clip_id BIGINT UNSIGNED NOT NULL,"
                "watch_time_sec INT UNSIGNED NOT NULL DEFAULT 0,"
                "max_position_sec INT UNSIGNED NOT NULL DEFAULT 0,"
                "completion_ratio FLOAT NOT NULL DEFAULT 0,"
                "early_skip TINYINT(1) NOT NULL DEFAULT 0,"
                "replay_count INT UNSIGNED NOT NULL DEFAULT 0,"
                "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
                "PRIMARY KEY (user_id, clip_id),"
                "CONSTRAINT fk_uce_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,"
                "CONSTRAINT fk_uce_clip FOREIGN KEY (clip_id) REFERENCES clips(id) ON DELETE CASCADE"
                ") ENGINE=InnoDB");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS user_feed_blocks ("
                "user_id BIGINT UNSIGNED NOT NULL,"
                "target_key VARCHAR(160) NOT NULL,"
                "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "PRIMARY KEY (user_id, target_key),"
                "CONSTRAINT fk_ufb_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
                ") ENGINE=InnoDB");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS feature_store_clip ("
                "clip_id BIGINT UNSIGNED PRIMARY KEY,"
                "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
                "age_hours FLOAT NOT NULL DEFAULT 0,"
                "velocity_score FLOAT NOT NULL DEFAULT 0,"
                "er_like FLOAT NOT NULL DEFAULT 0,"
                "freshness_decay FLOAT NOT NULL DEFAULT 0,"
                "CONSTRAINT fk_fsc_clip FOREIGN KEY (clip_id) REFERENCES clips(id) ON DELETE CASCADE"
                ") ENGINE=InnoDB");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS feature_store_user ("
                "user_id BIGINT UNSIGNED PRIMARY KEY,"
                "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
                "top_games_json JSON NULL,"
                "affinity_entropy FLOAT NULL,"
                "watch_sec_7d INT UNSIGNED NOT NULL DEFAULT 0,"
                "clips_viewed_7d INT UNSIGNED NOT NULL DEFAULT 0,"
                "CONSTRAINT fk_fsu_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
                ") ENGINE=InnoDB");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS analytics_sessions ("
                "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
                "user_id BIGINT UNSIGNED NOT NULL,"
                "session_id VARCHAR(64) NOT NULL,"
                "started_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "last_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
                "total_watch_sec INT UNSIGNED NOT NULL DEFAULT 0,"
                "clips_viewed INT UNSIGNED NOT NULL DEFAULT 0,"
                "UNIQUE KEY uq_analytics_session (user_id, session_id),"
                "INDEX idx_analytics_sess_user (user_id, last_seen_at),"
                "CONSTRAINT fk_as_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
                ") ENGINE=InnoDB");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS analytics_feed_events ("
                "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
                "user_id BIGINT UNSIGNED NOT NULL,"
                "session_id VARCHAR(64) DEFAULT NULL,"
                "event_type ENUM('watch_heartbeat','watch_session_end','home_impression') NOT NULL,"
                "block_type VARCHAR(32) NOT NULL DEFAULT '',"
                "clip_id BIGINT UNSIGNED DEFAULT NULL,"
                "value DOUBLE NULL,"
                "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "INDEX idx_afe_user_time (user_id, created_at),"
                "INDEX idx_afe_event (event_type, created_at),"
                "CONSTRAINT fk_afe_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
                ") ENGINE=InnoDB");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS analytics_user_daily ("
                "user_id BIGINT UNSIGNED NOT NULL,"
                "activity_date DATE NOT NULL,"
                "watch_sec INT UNSIGNED NOT NULL DEFAULT 0,"
                "clips_viewed INT UNSIGNED NOT NULL DEFAULT 0,"
                "PRIMARY KEY (user_id, activity_date),"
                "CONSTRAINT fk_aud_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
                ") ENGINE=InnoDB");
            if (!column_exists(conn.raw(), "users", "account_blocked", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE users ADD COLUMN account_blocked TINYINT(1) NOT NULL DEFAULT 0");
            if (!column_exists(conn.raw(), "users", "chat_banned_until", schema))
                mysql_query(conn.raw(), "ALTER TABLE users ADD COLUMN chat_banned_until TIMESTAMP NULL DEFAULT NULL");
            if (!column_exists(conn.raw(), "users", "chat_ban_reason", schema))
                mysql_query(conn.raw(), "ALTER TABLE users ADD COLUMN chat_ban_reason VARCHAR(512) NULL");
            if (!column_exists(conn.raw(), "users", "chat_banned_by", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE users ADD COLUMN chat_banned_by BIGINT UNSIGNED NULL");
            if (!column_exists(conn.raw(), "users", "legal_consent_at", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE users ADD COLUMN legal_consent_at VARCHAR(64) NULL DEFAULT NULL");
            if (!column_exists(conn.raw(), "users", "legal_consent_version", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE users ADD COLUMN legal_consent_version VARCHAR(32) NULL DEFAULT NULL");
            mysql_query(conn.raw(),
                "ALTER TABLE users MODIFY COLUMN `role` ENUM('user','moderator','admin','vip') NOT NULL DEFAULT 'user'");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS user_badge_history ("
                "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
                "user_id BIGINT UNSIGNED NOT NULL,"
                "badge VARCHAR(32) NOT NULL,"
                "event_type ENUM('granted','revoked') NOT NULL DEFAULT 'granted',"
                "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "INDEX idx_ubh_user (user_id, created_at DESC),"
                "CONSTRAINT fk_ubh_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
                ") ENGINE=InnoDB");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS content_reports ("
                "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
                "reporter_id BIGINT UNSIGNED NOT NULL,"
                "target_type ENUM('clip','comment','user') NOT NULL,"
                "target_id BIGINT UNSIGNED NOT NULL,"
                "reason VARCHAR(512) NOT NULL DEFAULT '',"
                "status ENUM('open','reviewed','dismissed') NOT NULL DEFAULT 'open',"
                "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "INDEX idx_cr_open (status, created_at DESC),"
                "CONSTRAINT fk_cr_reporter FOREIGN KEY (reporter_id) REFERENCES users(id) ON DELETE CASCADE"
                ") ENGINE=InnoDB");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS user_settings ("
                "user_id BIGINT UNSIGNED PRIMARY KEY,"
                "hide_mature TINYINT(1) NOT NULL DEFAULT 1,"
                "mute_keywords JSON NULL,"
                "clip_languages JSON NULL,"
                "CONSTRAINT fk_us_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
                ") ENGINE=InnoDB");
            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS clip_promotions ("
                "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
                "clip_id BIGINT UNSIGNED NOT NULL,"
                "boost_score DOUBLE NOT NULL DEFAULT 40,"
                "placement VARCHAR(24) NOT NULL DEFAULT 'all',"
                "label VARCHAR(64) NOT NULL DEFAULT '',"
                "starts_at TIMESTAMP NULL DEFAULT NULL,"
                "ends_at TIMESTAMP NULL DEFAULT NULL,"
                "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "INDEX idx_cp_clip (clip_id),"
                "INDEX idx_cp_place (placement),"
                "CONSTRAINT fk_cp_clip FOREIGN KEY (clip_id) REFERENCES clips(id) ON DELETE CASCADE"
                ") ENGINE=InnoDB");

            mysql_query(conn.raw(),
                "CREATE TABLE IF NOT EXISTS compliance_log ("
                "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
                "action VARCHAR(64) NOT NULL,"
                "clip_id BIGINT UNSIGNED NULL,"
                "actor_user_id BIGINT UNSIGNED NULL,"
                "details_json JSON NULL,"
                "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "INDEX idx_cl_clip (clip_id),"
                "INDEX idx_cl_time (created_at DESC)"
                ") ENGINE=InnoDB");
            if (!column_exists(conn.raw(), "clips", "is_mature", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE clips ADD COLUMN is_mature TINYINT(1) NOT NULL DEFAULT 0");
            if (!column_exists(conn.raw(), "clips", "unavailable_at", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE clips ADD COLUMN unavailable_at TIMESTAMP NULL DEFAULT NULL");
            if (!column_exists(conn.raw(), "clips", "removed_reason", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE clips ADD COLUMN removed_reason VARCHAR(64) NULL DEFAULT NULL");
            if (!column_exists(conn.raw(), "clips", "broadcaster_lang", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE clips ADD COLUMN broadcaster_lang VARCHAR(16) NOT NULL DEFAULT ''");
            if (!column_exists(conn.raw(), "user_settings", "clip_languages", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE user_settings ADD COLUMN clip_languages JSON NULL");
            /// STORED: дата клипа Twitch для индексируемых окон ленты (без STR_TO_DATE на всей таблице).
            if (!column_exists(conn.raw(), "clips", "twitch_created_ts", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE clips ADD COLUMN twitch_created_ts DATETIME GENERATED ALWAYS AS ("
                    "STR_TO_DATE(NULLIF(TRIM(twitch_created_at),''), '%Y-%m-%dT%H:%i:%sZ')) STORED");
            if (!index_exists(conn.raw(), "clips", "idx_clips_feed_ts", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE clips ADD INDEX idx_clips_feed_ts (twitch_created_ts, view_count)");
            if (!index_exists(conn.raw(), "view_history", "idx_vh_user_clip", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE view_history ADD INDEX idx_vh_user_clip (user_id, clip_id)");
            if (!column_exists(conn.raw(), "clips", "clip_creator_twitch_id", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE clips ADD COLUMN clip_creator_twitch_id VARCHAR(64) NOT NULL DEFAULT ''");
            if (!column_exists(conn.raw(), "clips", "clip_creator_login", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE clips ADD COLUMN clip_creator_login VARCHAR(128) NOT NULL DEFAULT ''");
            if (!index_exists(conn.raw(), "clips", "idx_clips_creator_tid", schema))
                mysql_query(conn.raw(),
                    "ALTER TABLE clips ADD INDEX idx_clips_creator_tid (clip_creator_twitch_id)");
        }
        std::cout << "[Main] Schema migrations applied\n";

        repositories::UserRepository    user_repo(db_pool);
        repositories::ClipRepository    clip_repo(db_pool);
        repositories::CommentRepository comment_repo(db_pool);

        clip_repo.refresh_feature_store_clips();
        clip_repo.refresh_feature_store_users();

        services::AuthService  auth_service(user_repo, jwt_secret, jwt_exp);
        services::CacheService cache_service(redis_host, redis_port);
        services::ClipService  clip_service(clip_repo, cache_service);

        services::TwitchService twitch_service(
            env("TWITCH_CLIENT_ID"), env("TWITCH_CLIENT_SECRET"), &cache_service);
        twitch_service.authenticate();

        auto threads = std::max(1u, std::thread::hardware_concurrency());
        boost::asio::io_context ioc(static_cast<int>(threads));

        services::SyncService sync_service(twitch_service, clip_repo, db_pool, ioc, &cache_service);
        sync_service.seed_games();
        // Don't block server start with Twitch sync (Redis lock clipstream:sync:lock — один активный синк при scale>1)
        std::thread([&sync_service] {
            try {
                sync_service.run_startup_sync();
            } catch (...) {}
        }).detach();
        const int sync_every = clip_sync_interval_minutes();
        std::cout << "[Main] Twitch clip sync interval: " << sync_every << " min (CLIP_SYNC_INTERVAL_MINUTES)\n";
        sync_service.start_periodic(sync_every);

        auto eventsub_broadcasters = split_csv(env("TWITCH_EVENTSUB_BROADCASTER_IDS", ""));
        services::EventSubService eventsub_service(twitch_service, cache_service, auth_service, eventsub_broadcasters);
        if (!eventsub_broadcasters.empty()) {
            eventsub_service.start();
            std::cout << "[Main] EventSub started for " << eventsub_broadcasters.size() << " broadcasters\n";
        } else {
            std::cout << "[Main] EventSub disabled: TWITCH_EVENTSUB_BROADCASTER_IDS is empty\n";
        }

        server::Router router;
        router.use(middleware::make_auth_middleware(auth_service));

        controllers::AuthController    auth_ctrl(auth_service, twitch_service, cache_service, redirect_uri);
        controllers::ClipController    clip_ctrl(clip_service, twitch_service, auth_service);
        controllers::ModerationController moderation_ctrl(auth_service, user_repo);
        controllers::CommentController comment_ctrl(comment_repo, auth_service, clip_repo, user_repo);
        controllers::EventSubController eventsub_ctrl(eventsub_service);
        controllers::StaffController    staff_ctrl(auth_service, cache_service, clip_repo);
        controllers::AdminController    admin_ctrl(auth_service, user_repo, clip_repo);
        controllers::SocialController   social_ctrl(auth_service, clip_service, user_repo);

        auth_ctrl.register_routes(router);
        clip_ctrl.register_routes(router);
        social_ctrl.register_routes(router);
        comment_ctrl.register_routes(router);
        eventsub_ctrl.register_routes(router);
        staff_ctrl.register_routes(router);
        admin_ctrl.register_routes(router);
        moderation_ctrl.register_routes(router);

        router.get("/health", [](const server::Request&) -> server::Response {
            return {200, {{"status", "ok"}}};
        });

        server::HttpServer srv(ioc, server_port, router);
        srv.start();

        std::vector<std::thread> workers;
        workers.reserve(threads - 1);
        for (unsigned i = 1; i < threads; ++i)
            workers.emplace_back([&ioc] { ioc.run(); });

        std::cout << "[Main] ClipStream backend started with " << threads << " threads\n";
        ioc.run();

        for (auto& t : workers) t.join();

    } catch (const std::exception& e) {
        std::cerr << "[Fatal] " << e.what() << "\n";
        return 1;
    }
}
