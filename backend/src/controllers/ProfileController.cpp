#include "controllers/ProfileController.hpp"
#include <algorithm>
#include <charconv>

namespace clipstream::controllers {
using json = nlohmann::json;
namespace {
server::Response denied() { return {401, {{"error", "Unauthorized"}}}; }
uint32_t number(const server::Request &req, const char *name, uint32_t fallback, uint32_t maximum) {
    auto it = req.query.find(name);
    if (it == req.query.end())
        return fallback;
    uint32_t value{};
    auto [end, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
    if (ec != std::errc{} || end != it->second.data() + it->second.size() || value > maximum)
        throw std::invalid_argument("Invalid pagination");
    return value;
}
} // namespace
void ProfileController::register_routes(server::Router &router) {
    router.get("/profiles/twitch/:id", [this](const auto &r) { return twitch_profile(r, false); });
    router.get("/profiles/twitch-login/:login", [this](const auto &r) { return twitch_profile(r, true); });
    router.get("/profiles/users/:username", [this](const auto &r) { return user_profile(r); });
    router.get("/profiles/:key", [this](const auto &r) { return keyed_profile(r); });
    router.get("/profiles/:key/clips", [this](const auto &r) { return profile_clips(r); });
    router.get("/profiles/:key/follow", [this](const auto &r) { return follow(r); });
    router.post("/profiles/:key/follow", [this](const auto &r) { return follow(r); });
    router.del("/profiles/:key/follow", [this](const auto &r) { return follow(r); });
    router.get("/profiles/:key/followers", [this](const auto &r) { return list(r, true, false); });
    router.get("/profiles/:key/following", [this](const auto &r) { return list(r, false, false); });
    router.get("/me/following", [this](const auto &r) { return list(r, false, true); });
    router.get("/me/following/feed", [this](const auto &r) { return following_feed(r); });
    router.get("/me/profile/settings", [this](const auto &r) { return settings(r); });
    router.put("/me/profile/settings", [this](const auto &r) { return settings(r); });
    router.put("/social/wall/:id", [this](const auto &r) { return wall_mutation(r); });
    router.del("/social/wall/:id", [this](const auto &r) { return wall_mutation(r); });
}
json ProfileController::present(const repositories::ProfileEntity &entity, uint64_t viewer, json twitch) {
    auto result = repositories::ProfileRepository::card(entity);
    auto owner = entity.user_id ? users_.find_by_id(entity.user_id) : std::nullopt;
    const bool self = owner && owner->id == viewer;
    const bool visible = profiles_.may_view(entity.user_id, viewer);
    const bool followers = visible && profiles_.may_view(entity.user_id, viewer, "followers_visibility");
    const bool following = visible && profiles_.may_view(entity.user_id, viewer, "following_visibility");
    auto friendship = owner ? users_.friendship_status(viewer, owner->id) : "none";
    if (friendship == "incoming")
        friendship = "pending_incoming";
    else if (friendship == "outgoing")
        friendship = "pending_outgoing";
    else if (friendship == "self")
        friendship = "none";
    result["relationship"] = {{"self", self},
                              {"following", profiles_.is_following(viewer, entity.key)},
                              {"friendship", friendship}};
    result["capabilities"] = {
        {"can_follow", viewer != 0 && !self && (!owner || !owner->account_blocked)},
        {"can_request_friend", viewer != 0 && owner && !self && !owner->account_blocked},
        {"can_view_profile", visible},
        {"can_view_followers", followers},
        {"can_view_following", following},
        {"can_view_activity", self},
        {"can_write_wall",
         owner && viewer && users_.may_post_comment(viewer) && profiles_.may_post_wall(owner->id, viewer)}};
    result["private"] = !visible;
    if (owner && result["avatar_url"] == "")
        result["avatar_url"] = owner->avatar_url;
    if (owner)
        result["local_user"] = {{"id", owner->id},
                                {"username", owner->username},
                                {"avatar_url", owner->avatar_url},
                                {"role", static_cast<int>(owner->role)}};
    if (visible) {
        auto stats = profiles_.stats(entity);
        if (!followers)
            stats["followers"] = nullptr;
        if (!following)
            stats["following"] = nullptr;
        result["stats"] = std::move(stats);
        if (owner) {
            auto prefs = profiles_.settings(owner->id);
            result["about"] = {{"display_name", prefs["display_name"]},
                               {"bio", prefs["bio"]},
                               {"website", prefs["website"]}};
            result["local_user"]["reputation"] = owner->reputation;
            result["local_user"]["badge"] = static_cast<int>(owner->badge);
        }
    } else
        result["stats"] = nullptr;
    if (!twitch.is_null())
        result["twitch"] = std::move(twitch);
    return result;
}
server::Response ProfileController::twitch_profile(const server::Request &req, bool by_login) {
    std::optional<repositories::ProfileEntity> existing;
    std::optional<services::TwitchUser> user;
    if (by_login)
        user = twitch_.fetch_user_by_login(req.params.at("login"));
    else {
        const auto id = req.params.at("id");
        if (id.empty() || id.size() > 64 ||
            !std::all_of(id.begin(), id.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
            return {400, {{"error", "Invalid Twitch ID"}}};
        existing = profiles_.find("t-" + id);
        if (existing && existing->age_seconds < 300 && !existing->login.empty())
            user =
                services::TwitchUser{id, existing->login, existing->display_name, "", existing->avatar_url};
        else
            try {
                user = twitch_.fetch_user_by_id(id);
            } catch (const services::TwitchError &) {
                if (existing) {
                    auto profile = present(*existing, req.user_id);
                    profile["metadata_stale"] = true;
                    return {200, {{"profile", profile}}};
                }
                throw;
            }
    }
    if (!user)
        return {404, {{"error", "Twitch profile not found"}}};
    auto entity = profiles_.ensure_twitch(user->id, user->login, user->display_name, user->profile_image_url);
    json channel = nullptr;
    try {
        auto raw = twitch_.get_public_channel_profile(user->login);
        if (raw.value("found", false))
            channel = raw["twitch"];
    } catch (const services::TwitchError &) {
        channel = {{"id", user->id},
                   {"login", user->login},
                   {"display_name", user->display_name},
                   {"profile_image_url", user->profile_image_url},
                   {"metadata_stale", true}};
    }
    return {200, {{"profile", present(entity, req.user_id, std::move(channel))}}};
}
server::Response ProfileController::user_profile(const server::Request &req) {
    auto user = users_.find_by_username(req.params.at("username"));
    if (!user)
        return {404, {{"error", "User not found"}}};
    return {200, {{"profile", present(profiles_.ensure_user(*user), req.user_id)}}};
}
server::Response ProfileController::keyed_profile(const server::Request &req) {
    const auto key = req.params.at("key");
    auto entity = profiles_.find(key);
    if (!entity && key.starts_with("t-")) {
        auto alias = req;
        alias.params["id"] = key.substr(2);
        return twitch_profile(alias, false);
    }
    if (!entity && key.starts_with("u-")) {
        uint64_t id{};
        const auto raw = key.substr(2);
        auto [end, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), id);
        if (ec == std::errc{} && end == raw.data() + raw.size())
            if (auto owner = users_.find_by_id(id))
                entity = profiles_.ensure_user(*owner);
    }
    if (!entity)
        return {404, {{"error", "Profile not found"}}};
    return {200, {{"profile", present(*entity, req.user_id)}}};
}
server::Response ProfileController::follow(const server::Request &req) {
    auto entity = profiles_.find(req.params.at("key"));
    if (!entity)
        return {404, {{"error", "Profile not found"}}};
    if (req.method == server::http::verb::get)
        return {200, {{"following", profiles_.is_following(req.user_id, entity->key)}}};
    if (!req.user_id)
        return denied();
    auto viewer = users_.find_by_id(req.user_id);
    if (!viewer)
        return denied();
    profiles_.ensure_user(*viewer);
    entity = profiles_.find(entity->key);
    if (!entity)
        return {404, {{"error", "Profile no longer exists"}}};
    if (entity->user_id == req.user_id)
        return {400, {{"error", "Cannot follow yourself"}}};
    if (entity->user_id && req.method == server::http::verb::post) {
        auto owner = users_.find_by_id(entity->user_id);
        if (owner && owner->account_blocked)
            return {403, {{"error", "Account unavailable"}}};
    }
    if (req.method == server::http::verb::post)
        profiles_.follow(req.user_id, entity->key);
    else
        profiles_.unfollow(req.user_id, entity->key);
    return {200,
            {{"profile", present(*entity, req.user_id)},
             {"following", profiles_.is_following(req.user_id, entity->key)}}};
}
server::Response ProfileController::list(const server::Request &req, bool followers, bool mine) {
    uint64_t owner_id = req.user_id;
    std::optional<repositories::ProfileEntity> entity;
    if (mine) {
        if (!req.user_id)
            return denied();
        auto owner = users_.find_by_id(req.user_id);
        if (!owner)
            return denied();
        entity = profiles_.ensure_user(*owner);
    } else {
        entity = profiles_.find(req.params.at("key"));
        if (!entity)
            return {404, {{"error", "Profile not found"}}};
        owner_id = entity->user_id;
        if (!profiles_.may_view(owner_id, req.user_id) ||
            !profiles_.may_view(owner_id, req.user_id,
                                followers ? "followers_visibility" : "following_visibility"))
            return {403, {{"error", "Private profile list"}}};
    }
    const auto limit = std::max(1u, number(req, "limit", 24, 100));
    const auto offset = number(req, "offset", 0, 100000);
    auto rows = followers ? profiles_.followers(entity->key, limit + 1, offset)
                          : profiles_.following(owner_id, limit + 1, offset);
    const bool more = rows.size() > limit;
    if (more)
        rows.resize(limit);
    const auto followed = profiles_.following_among(req.user_id, rows);
    json cards = json::array();
    for (const auto &row : rows) {
        auto card = repositories::ProfileRepository::card(row);
        card["relationship"] = {{"self", row.user_id != 0 && row.user_id == req.user_id},
                                {"following", mine || followed.contains(row.key)}};
        cards.push_back(std::move(card));
    }
    return {200, {{"profiles", cards}, {"has_more", more}, {"limit", limit}, {"offset", offset}}};
}
server::Response ProfileController::settings(const server::Request &req) {
    if (!req.user_id)
        return denied();
    if (req.method == server::http::verb::put)
        profiles_.save_settings(req.user_id, req.body);
    return {200, profiles_.settings(req.user_id)};
}
server::Response ProfileController::following_feed(const server::Request &req) {
    if (!req.user_id)
        return denied();
    const auto limit = std::max(1u, number(req, "limit", 20, 100));
    const auto offset = number(req, "offset", 0, 10000);
    std::optional<std::vector<std::string>> langs;
    auto it = req.query.find("clip_langs");
    if (it != req.query.end()) {
        langs = std::vector<std::string>{};
        std::string current;
        for (char c : it->second) {
            if (c == ',') {
                if (!current.empty())
                    langs->push_back(current);
                current.clear();
            } else
                current += c;
        }
        if (!current.empty())
            langs->push_back(current);
    }
    auto clips = clips_.get_following_feed(limit + 1, offset, req.user_id, langs);
    const bool more = clips.size() > limit;
    if (more)
        clips.resize(limit);
    return {200, {{"clips", clips}, {"has_more", more}, {"limit", limit}, {"offset", offset}}};
}
server::Response ProfileController::profile_clips(const server::Request &req) {
    auto profile = profiles_.find(req.params.at("key"));
    if (!profile)
        return {404, {{"error", "Profile not found"}}};
    if (!profiles_.may_view(profile->user_id, req.user_id))
        return {403, {{"error", "Private profile"}}};
    const auto limit = std::max(1u, number(req, "limit", 20, 100)), offset = number(req, "offset", 0, 10000);
    const auto it = req.query.find("scope");
    const auto scope = it == req.query.end() ? "all" : it->second;
    if (scope != "all" && scope != "channel" && scope != "created")
        return {400, {{"error", "Invalid clip scope"}}};
    std::optional<std::vector<std::string>> langs;
    auto language = req.query.find("clip_langs");
    if (language != req.query.end()) {
        langs = std::vector<std::string>{};
        std::string value;
        for (char c : language->second) {
            if (c == ',') {
                if (!value.empty())
                    langs->push_back(value);
                value.clear();
            } else
                value += c;
        }
        if (!value.empty())
            langs->push_back(value);
    }
    auto clips = clips_.get_entity_clips(profile->user_id, profile->twitch_id, scope, limit + 1, offset,
                                         req.user_id, langs);
    const bool more = clips.size() > limit;
    if (more)
        clips.resize(limit);
    return {200,
            {{"clips", clips}, {"has_more", more}, {"limit", limit}, {"offset", offset}, {"scope", scope}}};
}
server::Response ProfileController::wall_mutation(const server::Request &req) {
    if (!req.user_id)
        return denied();
    auto user = users_.find_by_id(req.user_id);
    if (!user)
        return denied();
    uint64_t post{};
    const auto &raw = req.params.at("id");
    auto [end, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), post);
    if (ec != std::errc{} || end != raw.data() + raw.size() || !post)
        return {400, {{"error", "Invalid post ID"}}};
    const bool remove = req.method == server::http::verb::delete_;
    std::string body;
    if (!remove) {
        if (!users_.may_post_comment(req.user_id))
            return {403, {{"error", "Posting is restricted"}}};
        if (!req.body.contains("body") || !req.body["body"].is_string())
            return {400, {{"error", "Missing body"}}};
        body = req.body["body"].get<std::string>();
    }
    const bool staff = user->role == models::UserRole::Moderator || user->role == models::UserRole::Admin;
    const auto status = profiles_.mutate_wall(post, req.user_id, staff, remove, body);
    if (status == repositories::ProfileRepository::WallMutation::NotFound)
        return {404, {{"error", "Post not found"}}};
    if (status == repositories::ProfileRepository::WallMutation::Forbidden)
        return {403, {{"error", "Post mutation is not permitted"}}};
    return {200,
            {{"ok", true},
             {"post_id", post},
             {"body", remove ? json(nullptr) : json(body)},
             {"mentions_notified", false}}};
}
} // namespace clipstream::controllers
