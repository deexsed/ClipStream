#include "controllers/NotificationController.hpp"

namespace clipstream::controllers {

NotificationController::NotificationController(repositories::NotificationRepository& repo) : repo_(repo) {}

void NotificationController::register_routes(server::Router& router) {
    router.get("/notifications",      [this](const server::Request& r) { return handle_list(r); });
    router.get("/notifications/unread-count", [this](const server::Request& r) { return handle_unread_count(r); });
    router.post("/notifications/read", [this](const server::Request& r) { return handle_mark_read(r); });
    router.post("/notifications/read-all", [this](const server::Request& r) { return handle_mark_all_read(r); });
}

server::Response NotificationController::handle_list(const server::Request& req) {
    if (req.user_id == 0) return {401, {{"error", "Unauthorized"}}};
    uint32_t limit = 30, offset = 0;
    auto lit = req.query.find("limit");
    if (lit != req.query.end()) {
        try {
            limit = static_cast<uint32_t>(std::stoul(lit->second));
        } catch (...) {}
    }
    auto oit = req.query.find("offset");
    if (oit != req.query.end()) {
        try {
            offset = static_cast<uint32_t>(std::stoul(oit->second));
        } catch (...) {}
    }
    if (limit > 100) limit = 100;
    auto rows = repo_.list_for_user(req.user_id, limit, offset);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) arr.push_back(r);
    return {200, {{"notifications", arr}}};
}

server::Response NotificationController::handle_unread_count(const server::Request& req) {
    if (req.user_id == 0) return {401, {{"error", "Unauthorized"}}};
    return {200, {{"unread", repo_.unread_count(req.user_id)}}};
}

server::Response NotificationController::handle_mark_read(const server::Request& req) {
    if (req.user_id == 0) return {401, {{"error", "Unauthorized"}}};
    std::vector<uint64_t> ids;
    if (req.body.contains("ids") && req.body["ids"].is_array()) {
        for (const auto& x : req.body["ids"]) {
            if (!x.is_number_unsigned()) continue;
            ids.push_back(x.get<uint64_t>());
        }
    }
    if (ids.empty()) return {400, {{"error", "Missing ids"}}};
    repo_.mark_read(req.user_id, ids);
    return {200, {{"ok", true}}};
}

server::Response NotificationController::handle_mark_all_read(const server::Request& req) {
    if (req.user_id == 0) return {401, {{"error", "Unauthorized"}}};
    repo_.mark_all_read(req.user_id);
    return {200, {{"ok", true}}};
}

} // namespace clipstream::controllers
