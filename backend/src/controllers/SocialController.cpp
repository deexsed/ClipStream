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
                                   services::TwitchService& twitch, repositories::UserRepository& users,
                                   repositories::NotificationRepository& notifications)
    : auth_(auth), clips_(clips), twitch_(twitch), users_(users), notifications_(notifications) {}

void SocialController::register_routes(server::Router& router) {
    router.get("/users/public/:username", [this](const server::Request& r) { return handle_public_profile(r); });
    router.post("/social/friends/request/:userId", [this](const server::Request& r) { return handle_friend_request_send(r); });
    router.del("/social/friends/request/:userId", [this](const server::Request& r) { return handle_friend_request_cancel(r); });
    router.post("/social/friends/accept/:userId", [this](const server::Request& r) { return handle_friend_accept(r); });
    router.post("/social/friends/decline/:userId", [this](const server::Request& r) { return handle_friend_decline(r); });
    router.del("/social/friends/:userId", [this](const server::Request& r) { return handle_friend_remove(r); });
    router.get("/social/users/:userId/friends", [this](const server::Request& r) { return handle_friends_list(r); });
    router.get("/social/friends/incoming", [this](const server::Request& r) { return handle_friend_requests_incoming(r); });
    router.get("/social/friends/outgoing", [this](const server::Request& r) { return handle_friend_requests_outgoing(r); });
    router.get("/user/settings", [this](const server::Request& r) { return handle_user_settings_get(r); });
    router.put("/user/settings", [this](const server::Request& r) { return handle_user_settings_put(r); });
    router.get("/social/friends-feed", [this](const server::Request& r) { return handle_friends_feed(r); });
    router.get("/social/users/:userId/followers", [this](const server::Request& r) { return handle_followers_list(r); });
    router.get("/social/users/:userId/following", [this](const server::Request& r) { return handle_following_list(r); });
    router.get("/social/users/:userId/clips", [this](const server::Request& r) { return handle_user_clips(r); });
    router.get("/social/users/:userId/watch-activity", [this](const server::Request& r) { return handle_watch_activity(r); });
    router.get("/users/search", [this](const server::Request& r) { return handle_user_search(r); });
    router.get("/users/:username/wall", [this](const server::Request& r) { return handle_wall_list(r); });
    router.post("/social/wall", [this](const server::Request& r) { return handle_wall_create(r); });
}

server::Response SocialController::handle_public_profile(const server::Request& req) {
    auto it = req.params.find("username");
    if (it == req.params.end()) return {400, {{"error", "Missing username"}}};
    auto profile = users_.get_public_profile(it->second, req.user_id);
    if (!profile) return {404, {{"error", "User not found"}}};
    nlohmann::json j = std::move(*profile);
    j["twitch_login"]    = nullptr;
    j["twitch_channel"] = nullptr;
    if (j.contains("twitch_id") && j["twitch_id"].is_string()) {
        std::string tid = j["twitch_id"].get<std::string>();
        if (!tid.empty()) {
            auto twu = twitch_.fetch_user_by_id(tid);
            if (twu && !twu->login.empty()) {
                j["twitch_login"] = twu->login;
                auto ch = twitch_.get_public_channel_profile(twu->login);
                const uint64_t n = clips_.count_clips_for_broadcaster(twu->login, twu->id);
                ch["clipstream_clips"] = n;
                j["twitch_channel"] = std::move(ch);
            }
        }
    }
    bool twitch_linked = false;
    if (j.contains("twitch_id") && !j["twitch_id"].is_null() && j["twitch_id"].is_string()) {
        const auto& tid = j["twitch_id"].get_ref<const std::string&>();
        twitch_linked = !tid.empty();
    }
    j["used_clipstream_via_twitch"] = twitch_linked;
    uint64_t uid = 0;
    if (j.contains("id") && j["id"].is_number()) {
        if (j["id"].is_number_unsigned()) {
            uid = j["id"].get<uint64_t>();
        } else {
            const int64_t x = j["id"].get<int64_t>();
            if (x > 0) uid = static_cast<uint64_t>(x);
        }
    }
    j["on_site_now"] = uid != 0 && users_.has_recent_site_activity(uid, 5);
    return {200, std::move(j)};
}

namespace {
uint64_t parse_user_id_param(const server::Request& req, const char* key) {
    auto it = req.params.find(key);
    if (it == req.params.end()) return 0;
    try {
        return std::stoull(it->second);
    } catch (...) {
        return 0;
    }
}
} // namespace

server::Response SocialController::handle_friend_request_send(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    uint64_t target = parse_user_id_param(req, "userId");
    if (target == 0) return {400, {{"error", "Invalid user id"}}};
    if (target == req.user_id) return {400, {{"error", "Cannot friend yourself"}}};
    if (!auth_.get_user(target)) return {404, {{"error", "User not found"}}};
    const int st = users_.send_friend_request(req.user_id, target);
    if (st < 0) return {400, {{"error", "Friend request failed"}}};
    if (st == 1) notifications_.notify_friend_request(target, req.user_id);
    if (st == 2) notifications_.notify_friend_accepted(req.user_id, target);
    return {200, {{"ok", true}, {"status", st}}};
}

server::Response SocialController::handle_friend_request_cancel(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    uint64_t target = parse_user_id_param(req, "userId");
    if (target == 0) return {400, {{"error", "Invalid user id"}}};
    users_.cancel_friend_request(req.user_id, target);
    return {200, {{"ok", true}}};
}

server::Response SocialController::handle_friend_accept(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    uint64_t requester = parse_user_id_param(req, "userId");
    if (requester == 0) return {400, {{"error", "Invalid user id"}}};
    if (!users_.accept_friend_request(req.user_id, requester)) return {400, {{"error", "No pending request"}}};
    notifications_.notify_friend_accepted(requester, req.user_id);
    return {200, {{"ok", true}}};
}

server::Response SocialController::handle_friend_decline(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    uint64_t requester = parse_user_id_param(req, "userId");
    if (requester == 0) return {400, {{"error", "Invalid user id"}}};
    users_.decline_friend_request(req.user_id, requester);
    return {200, {{"ok", true}}};
}

server::Response SocialController::handle_friend_remove(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    uint64_t target = parse_user_id_param(req, "userId");
    if (target == 0) return {400, {{"error", "Invalid user id"}}};
    users_.remove_friendship(req.user_id, target);
    return {200, {{"ok", true}}};
}

server::Response SocialController::handle_friends_list(const server::Request& req) {
    uint64_t uid = parse_user_id_param(req, "userId");
    if (uid == 0) return {400, {{"error", "Invalid user id"}}};
    if (!auth_.get_user(uid)) return {404, {{"error", "User not found"}}};
    uint32_t limit = 40, offset = 0;
    parse_limit_offset(req, limit, offset, 100);
    auto rows = users_.list_friends(uid, limit, offset);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) arr.push_back(r);
    return {200, {{"users", arr}, {"count", rows.size()}}};
}

server::Response SocialController::handle_friend_requests_incoming(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    uint32_t limit = 30, offset = 0;
    parse_limit_offset(req, limit, offset, 100);
    auto rows = users_.list_friend_requests_incoming(req.user_id, limit, offset);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) arr.push_back(r);
    return {200, {{"users", arr}, {"count", rows.size()}}};
}

server::Response SocialController::handle_friend_requests_outgoing(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    uint32_t limit = 30, offset = 0;
    parse_limit_offset(req, limit, offset, 100);
    auto rows = users_.list_friend_requests_outgoing(req.user_id, limit, offset);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) arr.push_back(r);
    return {200, {{"users", arr}, {"count", rows.size()}}};
}

server::Response SocialController::handle_wall_list(const server::Request& req) {
    auto it = req.params.find("username");
    if (it == req.params.end()) return {400, {{"error", "Missing username"}}};
    auto owner = users_.find_by_username(it->second);
    if (!owner) return {404, {{"error", "User not found"}}};
    uint32_t limit = 20, offset = 0;
    parse_limit_offset(req, limit, offset, 50);
    auto rows = users_.list_wall_posts(owner->id, limit, offset);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) arr.push_back(r);
    return {200, {{"posts", arr}, {"count", rows.size()}}};
}

server::Response SocialController::handle_wall_create(const server::Request& req) {
    if (req.user_id == 0) return unauthorized();
    if (!req.body.contains("username") || !req.body["username"].is_string())
        return {400, {{"error", "Missing username"}}};
    if (!req.body.contains("body") || !req.body["body"].is_string())
        return {400, {{"error", "Missing body"}}};
    std::string uname = req.body["username"].get<std::string>();
    std::string body  = req.body["body"].get<std::string>();
    auto owner = users_.find_by_username(uname);
    if (!owner) return {404, {{"error", "User not found"}}};
    if (owner->id != req.user_id) return {403, {{"error", "Only profile owner can post"}}};
    auto post_id = users_.create_wall_post(req.user_id, owner->id, body);
    if (!post_id) return {400, {{"error", "Invalid post"}}};
    auto targets = users_.wall_post_mentioned_user_ids(*post_id);
    for (uint64_t rid : targets) {
        if (rid != req.user_id)
            notifications_.notify_wall_mention(rid, req.user_id, *post_id, owner->username);
    }
    return {200, {{"ok", true}, {"post_id", *post_id}}};
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
    (void)req;
    return {200, {{"users", nlohmann::json::array()}, {"count", 0}}};
}

server::Response SocialController::handle_following_list(const server::Request& req) {
    (void)req;
    return {200, {{"users", nlohmann::json::array()}, {"count", 0}}};
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
