#include "controllers/ModerationController.hpp"
#include "services/CacheService.hpp"
#include <algorithm>
namespace clipstream::controllers {
namespace {
uint32_t page(const server::Request &req, const char *key, uint32_t fallback) {
    auto it = req.query.find(key);
    return it == req.query.end() ? fallback : static_cast<uint32_t>(std::stoul(it->second));
}
unsigned hours(const nlohmann::json &body, unsigned fallback) {
    if (body.contains("hours")) {
        if (!body["hours"].is_number_unsigned())
            throw std::invalid_argument("hours must be a positive integer");
        auto value = body["hours"].get<uint64_t>();
        if (value > 8760)
            throw std::invalid_argument("Maximum duration is 8760 hours");
        return static_cast<unsigned>(value);
    }
    if (body.contains("days")) {
        if (!body["days"].is_number_unsigned())
            throw std::invalid_argument("days must be a positive integer");
        auto value = body["days"].get<uint64_t>();
        if (value > 365)
            throw std::invalid_argument("Maximum duration is 365 days");
        return static_cast<unsigned>(value * 24);
    }
    return fallback;
}
} // namespace
ModerationController::ModerationController(repositories::AccountSecurityRepository &security,
                                           services::CacheService &cache)
    : security_(security), cache_(cache) {}
void ModerationController::register_routes(server::Router &router) {
    router.get("/moderation/users", [this](const server::Request &req) -> server::Response {
        if (!req.user_id)
            return {401, {{"error", "Unauthorized"}}};
        auto value = [&](const char *key) {
            auto it = req.query.find(key);
            return it == req.query.end() ? std::string{} : it->second;
        };
        return {200,
                security_.find_users(req.user_id, value("q"), value("status"),
                                     std::clamp(page(req, "limit", 30), 1u, 100u), page(req, "offset", 0))};
    });
    router.post("/moderation/reports/:id/resolve", [this](const server::Request &req) -> server::Response {
        if (!req.user_id)
            return {401, {{"error", "Unauthorized"}}};
        security_.resolve_report(req.user_id, std::stoull(req.params.at("id")),
                                 req.body.at("resolution").get<std::string>(),
                                 req.body.value("reason", std::string{}));
        return {200, {{"ok", true}}};
    });
    router.post("/moderation/clips/:id/visibility", [this](const server::Request &req) -> server::Response {
        if (!req.user_id)
            return {401, {{"error", "Unauthorized"}}};
        auto id = std::stoull(req.params.at("id"));
        security_.set_clip_visibility(req.user_id, id, req.body.at("hidden").get<bool>(),
                                      req.body.value("reason", std::string{}));
        cache_.del("clip:" + std::to_string(id));
        cache_.invalidate_pattern("clips:*");
        return {200, {{"ok", true}}};
    });
    router.post("/moderation/users/:id/restriction",
                [this](const auto &req) { return handle_restriction(req); });
    router.get("/moderation/users/:id", [this](const server::Request &req) -> server::Response {
        if (!req.user_id)
            return {401, {{"error", "Unauthorized"}}};
        return {200, security_.user_state(req.user_id, std::stoull(req.params.at("id")))};
    });
    router.get("/moderation/ip-bans", [this](const server::Request &req) -> server::Response {
        if (!req.user_id)
            return {401, {{"error", "Unauthorized"}}};
        auto limit = page(req, "limit", 30), offset = page(req, "offset", 0);
        auto rows = security_.ip_bans(req.user_id, limit, offset);
        return {200, {{"bans", rows}, {"has_more", rows.size() == limit}}};
    });
    router.post("/moderation/ip-bans", [this](const server::Request &req) -> server::Response {
        if (!req.user_id)
            return {401, {{"error", "Unauthorized"}}};
        auto id = security_.ban_ip(req.user_id, req.body.at("ip").get<std::string>(), hours(req.body, 0),
                                   req.body.value("reason", std::string{}), req.client_address);
        return {201, {{"id", id}, {"ok", true}}};
    });
    router.del("/moderation/ip-bans/:id", [this](const server::Request &req) -> server::Response {
        if (!req.user_id)
            return {401, {{"error", "Unauthorized"}}};
        security_.unban_ip(req.user_id, std::stoull(req.params.at("id")));
        return {200, {{"ok", true}}};
    });
    router.get("/moderation/audit", [this](const server::Request &req) -> server::Response {
        if (!req.user_id)
            return {401, {{"error", "Unauthorized"}}};
        auto limit = page(req, "limit", 30), offset = page(req, "offset", 0);
        auto rows = security_.audit(req.user_id, limit, offset);
        return {200, {{"events", rows}, {"has_more", rows.size() == limit}}};
    });
    router.post("/moderation/clips/:id/classification",
                [this](const server::Request &req) -> server::Response {
                    if (!req.user_id)
                        return {401, {{"error", "Unauthorized"}}};
                    security_.classify_clip(req.user_id, std::stoull(req.params.at("id")),
                                            req.body.at("is_mature").get<bool>(),
                                            req.body.value("reason", std::string{}));
                    cache_.invalidate_pattern("clips:*");
                    cache_.del("clip:" + req.params.at("id"));
                    return {200, {{"ok", true}}};
                });
}
server::Response ModerationController::handle_restriction(const server::Request &req) {
    if (!req.user_id)
        return {401, {{"error", "Unauthorized"}}};
    auto action = req.body.at("action").get<std::string>();
    auto duration = hours(req.body, action == "suspend_account" ? 24 : 168);
    if (action == "chat_ban" && req.body.value("permanent", false))
        duration = 0;
    security_.restrict_user(req.user_id, std::stoull(req.params.at("id")), action, duration,
                            req.body.value("reason", std::string{}));
    return {200, {{"ok", true}}};
}
} // namespace clipstream::controllers
