#include "controllers/AdminController.hpp"
#include "models/User.hpp"
#include <optional>

namespace clipstream::controllers {

namespace {

server::Response unauthorized() { return {401, {{"error", "Unauthorized"}}}; }

server::Response forbidden() { return {403, {{"error", "Admin access required"}}}; }

bool is_admin_user(const models::User& u) { return u.role == models::UserRole::Admin; }

std::optional<models::UserRole> parse_role_string(const std::string& s) {
    if (s == "admin") return models::UserRole::Admin;
    if (s == "moderator") return models::UserRole::Moderator;
    if (s == "vip") return models::UserRole::Vip;
    if (s == "user") return models::UserRole::User;
    return std::nullopt;
}

} // namespace

AdminController::AdminController(services::AuthService& auth, repositories::UserRepository& users,
                                 repositories::ClipRepository& clips)
    : auth_(auth), users_(users), clips_(clips) {}

void AdminController::register_routes(server::Router& router) {
    router.get("/admin/dashboard", [this](const server::Request& r) { return handle_dashboard(r); });
    router.get("/admin/users", [this](const server::Request& r) { return handle_list_users(r); });
    router.post("/admin/users/:id/role", [this](const server::Request& r) { return handle_set_role(r); });
    router.get("/admin/promotions", [this](const server::Request& r) { return handle_list_promotions(r); });
    router.post("/admin/promotions", [this](const server::Request& r) { return handle_create_promotion(r); });
    router.del("/admin/promotions/:id", [this](const server::Request& r) { return handle_delete_promotion(r); });
}

server::Response AdminController::handle_dashboard(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    auto actor = auth_.get_user(req.user_id);
    if (!actor || !is_admin_user(*actor)) return forbidden();

    nlohmann::json body = nlohmann::json::object();
    body["overview"] = users_.get_admin_overview_stats();
    body["analytics_24h"] = clips_.get_analytics_summary_24h();
    return {200, body};
}

server::Response AdminController::handle_list_users(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    auto actor = auth_.get_user(req.user_id);
    if (!actor || !is_admin_user(*actor)) return forbidden();

    uint32_t limit = 30, offset = 0;
    auto     it = req.query.find("limit");
    if (it != req.query.end()) {
        try {
            limit = static_cast<uint32_t>(std::stoul(it->second));
        } catch (...) {}
    }
    it = req.query.find("offset");
    if (it != req.query.end()) {
        try {
            offset = static_cast<uint32_t>(std::stoul(it->second));
        } catch (...) {}
    }
    if (limit < 1) limit = 1;
    if (limit > 100) limit = 100;

    std::string search;
    it = req.query.find("q");
    if (it != req.query.end()) search = it->second;

    auto total = users_.count_users_admin(search);
    auto list = users_.list_users_admin(limit, offset, search);

    return {200,
            {{"users", list},
             {"total", total},
             {"limit", limit},
             {"offset", offset}}};
}

server::Response AdminController::handle_set_role(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    auto actor = auth_.get_user(req.user_id);
    if (!actor || !is_admin_user(*actor)) return forbidden();

    if (!req.body.contains("role") || !req.body["role"].is_string())
        return {400, {{"error", "Missing string field 'role' (user|moderator|admin)"}}};

    uint64_t target_id = 0;
    try {
        target_id = std::stoull(req.params.at("id"));
    } catch (...) {
        return {400, {{"error", "Invalid user id"}}};
    }

    auto new_role = parse_role_string(req.body["role"].get<std::string>());
    if (!new_role) return {400, {{"error", "Invalid role"}}};

    auto target = users_.find_by_id(target_id);
    if (!target) return {404, {{"error", "User not found"}}};

    if (target_id == req.user_id && target->role == models::UserRole::Admin && *new_role != models::UserRole::Admin) {
        if (users_.count_by_role(models::UserRole::Admin) <= 1)
            return {400, {{"error", "Cannot demote the only admin"}}};
    }

    if (!users_.update_role(target_id, *new_role)) return {500, {{"error", "Failed to update role"}}};

    auto updated = users_.find_by_id(target_id);
    nlohmann::json u = nlohmann::json::object();
    if (updated) {
        std::string rs = "user";
        if (updated->role == models::UserRole::Admin) rs = "admin";
        else if (updated->role == models::UserRole::Moderator) rs = "moderator";
        else if (updated->role == models::UserRole::Vip) rs = "vip";
        u["id"] = updated->id;
        u["username"] = updated->username;
        u["email"] = updated->email;
        u["avatar_url"] = updated->avatar_url;
        u["role"] = rs;
    }
    return {200, {{"ok", true}, {"user", u}}};
}

server::Response AdminController::handle_list_promotions(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    auto actor = auth_.get_user(req.user_id);
    if (!actor || !is_admin_user(*actor)) return forbidden();

    uint32_t limit = 50, offset = 0;
    auto     it = req.query.find("limit");
    if (it != req.query.end()) {
        try {
            limit = static_cast<uint32_t>(std::stoul(it->second));
        } catch (...) {}
    }
    it = req.query.find("offset");
    if (it != req.query.end()) {
        try {
            offset = static_cast<uint32_t>(std::stoul(it->second));
        } catch (...) {}
    }
    if (limit < 1) limit = 1;
    if (limit > 200) limit = 200;

    auto rows = clips_.list_clip_promotions(limit, offset);
    return {200, {{"promotions", rows}, {"count", rows.size()}}};
}

server::Response AdminController::handle_create_promotion(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    auto actor = auth_.get_user(req.user_id);
    if (!actor || !is_admin_user(*actor)) return forbidden();

    if (!req.body.contains("clip_id") || !req.body["clip_id"].is_number_unsigned())
        return {400, {{"error", "clip_id required"}}};
    const uint64_t clip_id = req.body["clip_id"].get<uint64_t>();
    if (!clips_.find_by_id(clip_id)) return {404, {{"error", "Clip not found"}}};

    double boost = 45.0;
    if (req.body.contains("boost_score") && req.body["boost_score"].is_number())
        boost = req.body["boost_score"].get<double>();
    if (boost < 0) boost = 0;
    if (boost > 500) boost = 500;

    std::string placement = "all";
    if (req.body.contains("placement") && req.body["placement"].is_string())
        placement = req.body["placement"].get<std::string>();
    const auto ok_place = [](const std::string& p) {
        return p == "all" || p == "hot" || p == "fresh" || p == "recommended" || p == "feed" || p == "explore"
            || p == "related";
    };
    if (!ok_place(placement)) return {400, {{"error", "Invalid placement"}}};

    std::string label;
    if (req.body.contains("label") && req.body["label"].is_string()) label = req.body["label"].get<std::string>();
    if (label.size() > 64) label.resize(64);

    std::string starts_at, ends_at;
    if (req.body.contains("starts_at") && req.body["starts_at"].is_string())
        starts_at = req.body["starts_at"].get<std::string>();
    if (req.body.contains("ends_at") && req.body["ends_at"].is_string())
        ends_at = req.body["ends_at"].get<std::string>();

    auto new_id = clips_.create_clip_promotion(clip_id, boost, placement, label, starts_at, ends_at);
    if (!new_id) return {500, {{"error", "Failed to create promotion"}}};
    return {200, {{"ok", true}, {"id", *new_id}}};
}

server::Response AdminController::handle_delete_promotion(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    auto actor = auth_.get_user(req.user_id);
    if (!actor || !is_admin_user(*actor)) return forbidden();

    uint64_t pid = 0;
    try {
        pid = std::stoull(req.params.at("id"));
    } catch (...) {
        return {400, {{"error", "Invalid id"}}};
    }
    if (!clips_.delete_clip_promotion(pid)) return {404, {{"error", "Not found"}}};
    return {200, {{"ok", true}}};
}

} // namespace clipstream::controllers
