#include "controllers/ModerationController.hpp"
#include "models/User.hpp"

namespace clipstream::controllers {

namespace {

server::Response unauthorized() { return {401, {{"error", "Unauthorized"}}}; }

server::Response forbidden() { return {403, {{"error", "Moderator access required"}}}; }

bool is_mod(models::UserRole r) {
    return r == models::UserRole::Moderator || r == models::UserRole::Admin;
}

} // namespace

ModerationController::ModerationController(services::AuthService& auth, repositories::UserRepository& users)
    : auth_(auth), users_(users) {}

void ModerationController::register_routes(server::Router& router) {
    router.post("/moderation/users/:id/restriction", [this](const server::Request& r) { return handle_restriction(r); });
}

server::Response ModerationController::handle_restriction(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    auto actor = auth_.get_user(req.user_id);
    if (!actor || !is_mod(actor->role)) return forbidden();

    if (!req.body.contains("action") || !req.body["action"].is_string())
        return {400, {{"error", "Missing string field 'action'"}}};

    uint64_t target_id = 0;
    try {
        target_id = std::stoull(req.params.at("id"));
    } catch (...) {
        return {400, {{"error", "Invalid user id"}}};
    }

    const std::string action = req.body["action"].get<std::string>();
    std::string       reason;
    if (req.body.contains("reason") && req.body["reason"].is_string()) reason = req.body["reason"].get<std::string>();

    if (action == "lift_chat") {
        if (!users_.clear_chat_ban(target_id)) return {500, {{"error", "Failed"}}};
        return {200, {{"ok", true}}};
    }
    if (action == "block_account") {
        if (target_id == req.user_id) return {400, {{"error", "Cannot block yourself"}}};
        if (!users_.set_account_blocked(target_id, true)) return {500, {{"error", "Failed"}}};
        return {200, {{"ok", true}}};
    }
    if (action == "unblock_account") {
        if (!users_.set_account_blocked(target_id, false)) return {500, {{"error", "Failed"}}};
        return {200, {{"ok", true}}};
    }
    if (action == "chat_ban") {
        if (target_id == req.user_id) return {400, {{"error", "Cannot restrict yourself"}}};
        bool     permanent = req.body.contains("permanent") && req.body["permanent"].is_boolean()
                           && req.body["permanent"].get<bool>();
        uint32_t days      = 7;
        if (req.body.contains("days") && req.body["days"].is_number_unsigned())
            days = req.body["days"].get<uint32_t>();
        if (!users_.apply_chat_ban(target_id, permanent, days, reason, req.user_id))
            return {500, {{"error", "Failed"}}};
        return {200, {{"ok", true}}};
    }

    return {400, {{"error", "Unknown action"}}};
}

} // namespace clipstream::controllers
