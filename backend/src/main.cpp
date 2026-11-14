#include <boost/asio.hpp>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "database/ConnectionPool.hpp"
#include "database/Migrations.hpp"
#include "server/BlockingExecutor.hpp"
#include "server/HttpServer.hpp"
#include "server/Router.hpp"
#include "utils/Environment.hpp"
#include "utils/RuntimeConfig.hpp"

#include "repositories/AccountSecurityRepository.hpp"
#include "repositories/AdsRepository.hpp"
#include "repositories/ClipRepository.hpp"
#include "repositories/CommentRepository.hpp"
#include "repositories/NotificationRepository.hpp"
#include "repositories/UserRepository.hpp"

#include "services/AuthService.hpp"
#include "services/CacheService.hpp"
#include "services/ClipService.hpp"
#include "services/SyncService.hpp"
#include "services/TwitchService.hpp"

#include "controllers/AdminController.hpp"
#include "controllers/AdsController.hpp"
#include "controllers/AuthController.hpp"
#include "controllers/ClipController.hpp"
#include "controllers/CommentController.hpp"
#include "controllers/EventSubController.hpp"
#include "controllers/ModerationController.hpp"
#include "controllers/NotificationController.hpp"
#include "controllers/ProfileController.hpp"
#include "controllers/SocialController.hpp"
#include "controllers/StaffController.hpp"
#include "repositories/ProfileRepository.hpp"

#include "middleware/AuthMiddleware.hpp"

using namespace clipstream;
using utils::env;
namespace {
unsigned number(const char *key, unsigned fallback, unsigned maximum) {
    auto value = std::stoul(env(key, std::to_string(fallback)));
    if (value < 1 || value > maximum)
        throw std::invalid_argument(std::string("Invalid configuration: ") + key);
    return static_cast<unsigned>(value);
}
} // namespace
int main() {
    try {
        const auto role = env("CLIPSTREAM_ROLE", "api");
        if (role != "api" && role != "worker" && role != "migrate")
            throw std::invalid_argument("CLIPSTREAM_ROLE must be api, worker or migrate");
        database::DbConfig db_cfg{
            .host = env("MYSQL_HOST", "127.0.0.1"),
            .port = static_cast<uint16_t>(number("MYSQL_PORT", 3306, 65535)),
            .database = env("MYSQL_DATABASE", "clipstream"),
            .user = env("MYSQL_USER", "clipstream"),
            .password = env("MYSQL_PASSWORD"),
            .pool_size = role == "migrate" ? 1u : number("MYSQL_POOL_SIZE", 12, 128),
            .read_timeout_seconds = role == "migrate" ? number("MYSQL_MIGRATION_TIMEOUT", 300, 3600) : 5u,
            .write_timeout_seconds = role == "migrate" ? number("MYSQL_MIGRATION_TIMEOUT", 300, 3600) : 5u};
        database::ConnectionPool db_pool(db_cfg);
        if (role == "migrate") {
            database::migrate(db_pool, env("MIGRATIONS_DIR", "database/migrations"));
            return 0;
        }
        if (!database::schema_ready(db_pool))
            throw std::runtime_error("Run the migration role before starting application services");
        (void)env("PROXY_AUTH_SECRET"); // Fail startup on an unreadable configured gateway secret.
        auto jwt_secret = env("JWT_SECRET");
        if (jwt_secret.size() < 32)
            throw std::invalid_argument("JWT_SECRET must contain at least 32 characters");
        repositories::UserRepository user_repo(db_pool);
        repositories::ProfileRepository profile_repo(db_pool);
        repositories::AccountSecurityRepository security_repo(db_pool);
        repositories::ClipRepository clip_repo(db_pool);
        repositories::CommentRepository comment_repo(db_pool);
        repositories::NotificationRepository notif_repo(db_pool);
        repositories::AdsRepository ads_repo(db_pool);
        services::CacheService cache_service(env("REDIS_HOST", "127.0.0.1"),
                                             static_cast<uint16_t>(number("REDIS_PORT", 6379, 65535)));
        services::ClipService clip_service(clip_repo, user_repo, cache_service);
        services::TwitchService twitch_service(env("TWITCH_CLIENT_ID"), env("TWITCH_CLIENT_SECRET"),
                                               &cache_service);
        services::AuthService auth_service(user_repo, jwt_secret, number("JWT_EXPIRATION_HOURS", 24, 168));
        auth_service.bind_twitch(twitch_service, cache_service);
        twitch_service.set_user_token_refresh_handler(
            [&](const std::string &token) { return auth_service.resolve_user_token(token); });
        boost::asio::io_context ioc;
        services::SyncService sync_service(twitch_service, clip_repo, db_pool, ioc, &cache_service);
        std::vector<std::string> broadcasters;
        std::istringstream ids(env("TWITCH_EVENTSUB_BROADCASTER_IDS"));
        std::string id;
        while (std::getline(ids, id, ','))
            if (!id.empty())
                broadcasters.push_back(id);
        std::function<std::string()> eventsub_token;
        if (!env("TWITCH_EVENTSUB_USER_ID").empty())
            eventsub_token = [&] {
                return auth_service.resolve_user_token_by_twitch_id(env("TWITCH_EVENTSUB_USER_ID"));
            };
        services::EventSubService eventsub_service(twitch_service, cache_service, auth_service, broadcasters,
                                                   eventsub_token);
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&](const auto &error, int) {
            if (!error)
                ioc.stop();
        });
        if (role == "worker") {
            auth_service.start_validation();
            const bool poll_twitch=!env("TWITCH_CLIENT_ID").empty() && !env("TWITCH_CLIENT_SECRET").empty();
            if (poll_twitch) {
                sync_service.seed_games();
                if (!broadcasters.empty())
                    eventsub_service.start();
            }
            sync_service.start_periodic(number("CLIP_SYNC_INTERVAL_MINUTES", 1, 120),poll_twitch);
            std::cout << "[Worker] Started\n";
            ioc.run();
            eventsub_service.stop();
            sync_service.stop();
            auth_service.stop_validation();
            return 0;
        }
        server::Router router;
        router.use([&](server::Request &req, server::Response &response) {
            if (req.path.starts_with("/health") || req.path == "/config")
                return true;
            if (security_repo.is_ip_banned(req.client_address)) {
                response = {403, {{"error", "IP address is restricted"}}};
                return false;
            }
            return true;
        });
        router.use(middleware::make_auth_middleware(auth_service));
        // Mutation quotas live in Redis and apply across API replicas.
        router.use([&](server::Request &req, server::Response &response) {
            if (req.method == server::http::verb::get && req.path != "/ads/serve")
                return true;
            const auto key =
                "api:mutations:" + (req.user_id ? std::to_string(req.user_id) : req.client_address);
            if (!cache_service.acquire_rate_slot(key, 120, 60)) {
                response = {429, {{"error", "Too many requests"}, {"retry_after", 60}}};
                return false;
            }
            return true;
        });
        controllers::AuthController auth_ctrl(auth_service, twitch_service, cache_service,
                                              env("TWITCH_REDIRECT_URI", "http://localhost/auth/callback"),
                                              security_repo);
        controllers::ClipController clip_ctrl(clip_service, twitch_service, auth_service);
        controllers::ModerationController moderation_ctrl(security_repo, cache_service);
        controllers::CommentController comment_ctrl(comment_repo, auth_service, clip_repo, user_repo,
                                                    notif_repo);
        controllers::EventSubController eventsub_ctrl(eventsub_service);
        controllers::StaffController staff_ctrl(auth_service, cache_service, clip_repo);
        repositories::SyncRepository sync_repo(db_pool);
        controllers::AdminController admin_ctrl(auth_service, user_repo, clip_repo, sync_repo, twitch_service);
        controllers::AdsController ads_ctrl(auth_service, ads_repo, jwt_secret);
        controllers::SocialController social_ctrl(auth_service, clip_service, twitch_service, user_repo,
                                                  notif_repo, profile_repo);
        controllers::ProfileController profile_ctrl(profile_repo, user_repo, twitch_service, clip_service);
        controllers::NotificationController notif_ctrl(notif_repo);
        auth_ctrl.register_routes(router);
        clip_ctrl.register_routes(router);
        social_ctrl.register_routes(router);
        profile_ctrl.register_routes(router);
        comment_ctrl.register_routes(router);
        notif_ctrl.register_routes(router);
        eventsub_ctrl.register_routes(router);
        staff_ctrl.register_routes(router);
        admin_ctrl.register_routes(router);
        ads_ctrl.register_routes(router);
        moderation_ctrl.register_routes(router);
        router.get("/config", [](const server::Request &) -> server::Response {
            return {200, utils::runtime_config()};
        });
        router.get("/health/live",
                   [](const server::Request &) -> server::Response { return {200, {{"status", "alive"}}}; });
        auto ready = [&](const server::Request &) -> server::Response {
            bool okay = database::schema_ready(db_pool) && cache_service.ping_redis();
            return {okay ? 200u : 503u, {{"status", okay ? "ready" : "unavailable"}}};
        };
        router.get("/health", ready);
        router.get("/health/ready", ready);
        server::BlockingExecutor executor(number("HTTP_WORKERS", 8, 64),
                                          number("HTTP_QUEUE_CAPACITY", 64, 1024));
        server::HttpServer server(ioc, static_cast<uint16_t>(number("BACKEND_PORT", 8080, 65535)), router,
                                  executor);
        server.start();
        std::cout << "[API] Ready\n";
        ioc.run();
        server.stop();
        executor.stop();
        executor.join();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "[Fatal] " << error.what() << "\n";
        return 1;
    }
}
