#include "controllers/SocialController.hpp"
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace clipstream::controllers {

namespace {

std::optional<std::vector<std::string>> parse_clip_langs_query(const server::Request& req) {
    auto it = req.query.find("clip_langs");
    if (it == req.query.end()) return std::nullopt;
    std::vector<std::string> out;
    std::string cur;
    for (char ch : it->second) {
        if (ch == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else if (ch != ' ' && ch != '\t')
            cur += ch;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

server::Response unauthorized() { return {401, {{"error", "Unauthorized"}}}; }

void parse_limit_offset(const server::Request& req, uint32_t& limit, uint32_t& offset, uint32_t max_limit) {
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
    if (limit > max_limit) limit = max_limit;
    if (limit < 1) limit = 1;
}

} // namespace

SocialController::SocialController(services::AuthService& auth, services::ClipService& clips,
                                   repositories::UserRepository& users)
    : auth_(auth), clips_(clips), users_(users) {}

void SocialController::register_routes(server::Router& router) {
    router.get("/users/public/:username", [this](const server::Request& r) { return handle_public_profile(r); });
    router.post("/social/follow/:userId", [this](const server::Request& r) { return handle_follow(r); });
    router.del("/social/follow/:userId", [this](const server::Request& r) { return handle_unfollow(r); });
    router.get("/user/settings", [this](const server::Request& r) { return handle_user_settings_get(r); });
    router.put("/user/settings", [this](const server::Request& r) { return handle_user_settings_put(r); });
    router.get("/social/friends-feed", [this](const server::Request& r) { return handle_friends_feed(r); });
    router.get("/social/users/:userId/followers", [this](const server::Request& r) { return handle_followers_list(r); });
    router.get("/social/users/:userId/following", [this](const server::Request& r) { return handle_following_list(r); });
    router.get("/social/users/:userId/clips", [this](const server::Request& r) { return handle_user_clips(r); });
    router.get("/social/users/:userId/watch-activity", [this](const server::Request& r) { return handle_watch_activity(r); });
    router.get("/users/search", [this](const server::Request& r) { return handle_user_search(r); });
}

server::Response SocialController::handle_public_profile(const server::Request& req) {
    auto it = req.params.find("username");
    if (it == req.params.end()) return {400, {{"error", "Missing username"}}};
    auto profile = users_.get_public_profile(it->second, req.user_id);
    if (!profile) return {404, {{"error", "User not found"}}};
    return {200, *profile};
}

server::Response SocialController::handle_follow(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    auto it = req.params.find("userId");
    if (it == req.params.end()) return {400, {{"error", "Missing user id"}}};
    uint64_t target = 0;
    try {
        target = std::stoull(it->second);
    } catch (...) {
        return {400, {{"error", "Invalid user id"}}};
    }
    if (target == req.user_id) return {400, {{"error", "Cannot follow yourself"}}};
    if (!auth_.get_user(target)) return {404, {{"error", "User not found"}}};
    if (!users_.follow(req.user_id, target)) return {400, {{"error", "Follow failed"}}};
    return {200, {{"ok", true}}};
}

server::Response SocialController::handle_unfollow(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    auto it = req.params.find("userId");
    if (it == req.params.end()) return {400, {{"error", "Missing user id"}}};
    uint64_t target = 0;
    try {
        target = std::stoull(it->second);
    } catch (...) {
        return {400, {{"error", "Invalid user id"}}};
    }
    users_.unfollow(req.user_id, target);
    return {200, {{"ok", true}}};
}

server::Response SocialController::handle_user_settings_get(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    return {200, users_.get_user_settings(req.user_id)};
}

server::Response SocialController::handle_user_settings_put(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    const bool hide_mature = req.body.value("hide_mature", true);
    std::vector<std::string> keywords;
    if (req.body.contains("mute_keywords") && req.body["mute_keywords"].is_array()) {
        for (const auto& item : req.body["mute_keywords"]) {
            if (!item.is_string()) continue;
            keywords.push_back(item.get<std::string>());
        }
    }
    std::vector<std::string> clip_langs;
    if (req.body.contains("clip_languages") && req.body["clip_languages"].is_array()) {
        for (const auto& item : req.body["clip_languages"]) {
            if (!item.is_string()) continue;
            clip_langs.push_back(item.get<std::string>());
        }
    }
    if (!users_.save_user_settings(req.user_id, hide_mature, keywords, clip_langs))
        return {400, {{"error", "Failed to save settings"}}};
    clips_.invalidate_feed_cache_for_user(req.user_id);
    return {200, users_.get_user_settings(req.user_id)};
}

server::Response SocialController::handle_friends_feed(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    uint32_t limit = 20, offset = 0;
    parse_limit_offset(req, limit, offset, 50);
    auto clips = clips_.get_friends_feed(limit, offset, req.user_id, parse_clip_langs_query(req));
    nlohmann::json j = nlohmann::json::array();
    for (auto& c : clips) j.push_back(c);
    return {200, {{"clips", j}, {"count", clips.size()}}};
}

server::Response SocialController::handle_followers_list(const server::Request& req) {
    auto it = req.params.find("userId");
    if (it == req.params.end()) return {400, {{"error", "Missing user id"}}};
    uint64_t uid = 0;
    try {
        uid = std::stoull(it->second);
    } catch (...) {
        return {400, {{"error", "Invalid user id"}}};
    }
    if (!auth_.get_user(uid)) return {404, {{"error", "User not found"}}};
    uint32_t limit = 30, offset = 0;
    parse_limit_offset(req, limit, offset, 100);
    auto rows = users_.list_followers(uid, limit, offset);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) arr.push_back(r);
    return {200, {{"users", arr}, {"count", rows.size()}}};
}

server::Response SocialController::handle_following_list(const server::Request& req) {
    auto it = req.params.find("userId");
    if (it == req.params.end()) return {400, {{"error", "Missing user id"}}};
    uint64_t uid = 0;
    try {
        uid = std::stoull(it->second);
    } catch (...) {
        return {400, {{"error", "Invalid user id"}}};
    }
    if (!auth_.get_user(uid)) return {404, {{"error", "User not found"}}};
    uint32_t limit = 30, offset = 0;
    parse_limit_offset(req, limit, offset, 100);
    auto rows = users_.list_following(uid, limit, offset);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) arr.push_back(r);
    return {200, {{"users", arr}, {"count", rows.size()}}};
}

server::Response SocialController::handle_user_clips(const server::Request& req) {
    auto it = req.params.find("userId");
    if (it == req.params.end()) return {400, {{"error", "Missing user id"}}};
    uint64_t uid = 0;
    try {
        uid = std::stoull(it->second);
    } catch (...) {
        return {400, {{"error", "Invalid user id"}}};
    }
    auto owner = auth_.get_user(uid);
    if (!owner) return {404, {{"error", "User not found"}}};
    uint32_t limit = 24, offset = 0;
    parse_limit_offset(req, limit, offset, 50);
    const std::string twitch_id = owner->twitch_id.empty() ? "" : owner->twitch_id;
    auto clips = clips_.get_profile_clips(uid, twitch_id, limit, offset, req.user_id, parse_clip_langs_query(req));
    nlohmann::json j = nlohmann::json::array();
    for (auto& c : clips) j.push_back(c);
    return {200, {{"clips", j}, {"count", clips.size()}}};
}

server::Response SocialController::handle_watch_activity(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    auto it = req.params.find("userId");
    if (it == req.params.end()) return {400, {{"error", "Missing user id"}}};
    uint64_t uid = 0;
    try {
        uid = std::stoull(it->second);
    } catch (...) {
        return {400, {{"error", "Invalid user id"}}};
    }
    if (uid != req.user_id) return {403, {{"error", "Private activity"}}};
    uint32_t limit = 30, offset = 0;
    parse_limit_offset(req, limit, offset, 100);
    auto rows = clips_.get_user_watch_activity(uid, limit, offset);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) arr.push_back(r);
    return {200, {{"activity", arr}, {"count", rows.size()}}};
}

server::Response SocialController::handle_user_search(const server::Request& req) {
    auto qit = req.query.find("q");
    if (qit == req.query.end() || qit->second.size() < 2)
        return {400, {{"error", "Query min length 2"}}};
    uint32_t limit = 15;
    auto lit = req.query.find("limit");
    if (lit != req.query.end()) {
        try {
            limit = static_cast<uint32_t>(std::stoul(lit->second));
        } catch (...) {}
    }
    if (limit > 50) limit = 50;
    auto rows = users_.search_users_by_username_prefix(qit->second, limit);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) arr.push_back(r);
    return {200, {{"users", arr}}};
}

} // namespace clipstream::controllers
