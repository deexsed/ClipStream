#include "controllers/ClipController.hpp"
#include "models/User.hpp"
#include "utils/Utf8.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace clipstream::controllers {

namespace {

std::optional<std::vector<std::string>> parse_clip_langs_query(const server::Request &req) {
    auto it = req.query.find("clip_langs");
    if (it == req.query.end())
        return std::nullopt;
    std::vector<std::string> out;
    std::string cur;
    for (char ch : it->second) {
        if (ch == ',') {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        } else if (ch != ' ' && ch != '\t')
            cur += ch;
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

} // namespace

ClipController::ClipController(services::ClipService &clip_service, services::TwitchService &twitch_service,
                               services::AuthService &auth_service)
    : service_(clip_service), twitch_(twitch_service), auth_(auth_service) {}

void ClipController::register_routes(server::Router &router) {
    router.get("/games", [this](const server::Request &r) { return handle_games(r); });
    router.get("/streams/live", [this](const server::Request &r) { return handle_live_streams(r); });
    router.get("/streams/check", [this](const server::Request &r) { return handle_streams_check(r); });
    router.get("/channels/search", [this](const server::Request &r) { return handle_channel_search(r); });
    router.get("/channels/followed",
               [this](const server::Request &r) { return handle_followed_channels(r); });
    router.get("/channels/metadata",
               [this](const server::Request &r) { return handle_channels_metadata(r); });
    router.get("/channels/similar", [this](const server::Request &r) { return handle_similar_channels(r); });
    router.get("/channels/profile", [this](const server::Request &r) { return handle_channel_profile(r); });
    router.get("/clips/home", [this](const server::Request &r) { return handle_home_sections(r); });
    router.get("/clips/home-feed", [this](const server::Request &r) { return handle_home_feed(r); });
    router.post("/clips/impressions", [this](const server::Request &r) {
        if (!r.body.contains("delivery_ids") || !r.body["delivery_ids"].is_array())
            return server::Response{400, {{"error", "Missing delivery_ids"}}};
        service_.record_recommendation_impressions(r.user_id,r.body["delivery_ids"].get<std::vector<std::string>>());
        return server::Response{200, {{"ok", true}}};
    });
    router.get("/admin/analytics/summary",
               [this](const server::Request &r) { return handle_admin_analytics_summary(r); });
    router.get("/clips", [this](const server::Request &r) { return handle_get_feed(r); });
    router.get("/clips/search", [this](const server::Request &r) { return handle_search(r); });
    router.get("/clips/discussed", [this](const server::Request &r) { return handle_discussed_clips(r); });
    router.get("/clips/continue", [this](const server::Request &r) { return handle_continue_watching(r); });
    router.get("/clips/channel-updates",
               [this](const server::Request &r) { return handle_channel_updates(r); });
    router.get("/clips/random", [this](const server::Request &r) { return handle_random_clip(r); });
    router.get("/clips/:id/trend-ranks",
               [this](const server::Request &r) { return handle_clip_trend_ranks(r); });
    router.get("/clips/:id/related", [this](const server::Request &r) { return handle_related_clips(r); });
    router.get("/clips/:id", [this](const server::Request &r) { return handle_get_clip(r); });
    router.post("/clips/:id/view", [this](const server::Request &r) { return handle_view(r); });
    router.post("/clips/:id/engagement",
                [this](const server::Request &r) { return handle_clip_engagement(r); });
    router.post("/clips/:id/feedback", [this](const server::Request &r) { return handle_clip_feedback(r); });
    router.post("/clips/:id/like", [this](const server::Request &r) { return handle_like(r); });
    router.post("/clips/:id/report", [this](const server::Request &r) { return handle_report_clip(r); });
}

server::Response ClipController::handle_get_feed(const server::Request &req) {
    repositories::ClipFilter filter;

    auto it = req.query.find("game_id");
    if (it != req.query.end())
        filter.game_id = std::stoull(it->second);

    it = req.query.find("sort");
    if (it != req.query.end()) {
        if (it->second == "recent")
            filter.sort = repositories::ClipSort::Recent;
        else if (it->second == "duration")
            filter.sort = repositories::ClipSort::Duration;
        else
            filter.sort = repositories::ClipSort::Popular;
    }

    it = req.query.find("period");
    if (it != req.query.end()) {
        if (it->second == "week")
            filter.period = repositories::TimePeriod::Week;
        else if (it->second == "month")
            filter.period = repositories::TimePeriod::Month;
        else if (it->second == "all")
            filter.period = repositories::TimePeriod::All;
        else
            filter.period = repositories::TimePeriod::Today;
    }

    it = req.query.find("tag");
    if (it != req.query.end())
        filter.tag = it->second;
    it = req.query.find("channel_login");
    if (it != req.query.end())
        filter.broadcaster_name = it->second;
    it = req.query.find("search");
    if (it != req.query.end())
        filter.search = it->second;

    uint32_t limit = 20, offset = 0;
    it = req.query.find("limit");
    if (it != req.query.end())
        limit = static_cast<uint32_t>(std::stoul(it->second));
    it = req.query.find("offset");
    if (it != req.query.end())
        offset = static_cast<uint32_t>(std::stoul(it->second));

    bool friends_feed = req.query.find("friends") != req.query.end() && req.query.at("friends") == "1";
    bool personalized =
        req.query.find("personalized") != req.query.end() && req.query.at("personalized") == "1";
    // Personalization should only shape the generic home feed.
    // For contextual feeds (category/channel/tag/search), explicit filters must take precedence.
    if (filter.game_id > 0 || !filter.broadcaster_name.empty() || !filter.tag.empty() ||
        !filter.search.empty()) {
        personalized = false;
    }
    const auto clip_langs = parse_clip_langs_query(req);
    std::vector<models::Clip> clips;
    if (friends_feed && req.user_id != 0) {
        clips = service_.get_friends_feed(limit, offset, req.user_id, clip_langs);
        nlohmann::json j = nlohmann::json::array();
        for (auto &c : clips)
            j.push_back(c);
        return {200, {{"clips", j}, {"count", clips.size()}}};
    }
    if (personalized && req.user_id != 0) {
        auto user = auth_.get_user(req.user_id);
        if (user && !user->twitch_access_token.empty() && !user->twitch_id.empty()) {
            std::string access_token = auth_.user_access_token(user->id);

            auto followed = twitch_.get_followed_channels(access_token, user->twitch_id, 100);
            std::vector<std::string> names;
            names.reserve(followed.size());
            for (const auto &ch : followed) {
                auto name = ch.value("broadcaster_login", std::string{});
                if (name.empty())
                    name = ch.value("broadcaster_name", std::string{});
                if (!name.empty())
                    names.push_back(name);
            }
            clips = service_.get_personalized_feed(filter, names, limit, offset, req.user_id, clip_langs);
        } else {
            clips = service_.get_feed(filter, limit, offset, req.user_id, clip_langs);
        }
    } else {
        clips = service_.get_feed(filter, limit, offset, req.user_id, clip_langs);
    }

    nlohmann::json j = nlohmann::json::array();
    for (auto &c : clips)
        j.push_back(c);
    return {200, {{"clips", j}, {"count", clips.size()}}};
}

server::Response ClipController::handle_clip_trend_ranks(const server::Request &req) {
    auto it = req.params.find("id");
    if (it == req.params.end())
        return {400, {{"error", "Missing clip id"}}};
    uint64_t clip_id = 0;
    try {
        clip_id = std::stoull(it->second);
    } catch (...) {
        return {400, {{"error", "Invalid clip id"}}};
    }
    auto j = service_.get_clip_trend_ranks(clip_id, req.user_id);
    if (!j)
        return {404, {{"error", "Clip not found"}}};
    return {200, *j};
}

server::Response ClipController::handle_get_clip(const server::Request &req) {
    auto it = req.params.find("id");
    if (it == req.params.end())
        return {400, {{"error", "Missing clip id"}}};

    uint64_t id = std::stoull(it->second);
    auto j = service_.get_clip_json_for_viewer(id, req.user_id);
    if (!j)
        return {404, {{"error", "Clip not found"}}};
    return {200, *j};
}

server::Response ClipController::handle_related_clips(const server::Request &req) {
    auto it = req.params.find("id");
    if (it == req.params.end())
        return {400, {{"error", "Missing clip id"}}};
    uint64_t seed_id = 0;
    try {
        seed_id = std::stoull(it->second);
    } catch (...) {
        return {400, {{"error", "Invalid clip id"}}};
    }
    uint32_t limit = 12;
    auto lit = req.query.find("limit");
    if (lit != req.query.end()) {
        try {
            limit = static_cast<uint32_t>(std::stoul(lit->second));
        } catch (...) {
        }
    }
    if (!service_.get_clip(seed_id))
        return {404, {{"error", "Clip not found"}}};

    uint32_t mix_seed = 0;
    auto mit = req.query.find("mix");
    if (mit != req.query.end()) {
        try {
            mix_seed = static_cast<uint32_t>(std::stoul(mit->second));
        } catch (...) {
        }
    }
    if (mix_seed == 0)
        mix_seed = static_cast<uint32_t>(seed_id ^ (seed_id << 13U));

    std::vector<uint64_t> exclude_ids;
    auto eit = req.query.find("exclude");
    if (eit != req.query.end()) {
        std::string cur;
        for (char ch : eit->second) {
            if (ch == ',' || ch == ' ') {
                if (!cur.empty()) {
                    try {
                        exclude_ids.push_back(std::stoull(cur));
                    } catch (...) {
                    }
                    cur.clear();
                }
            } else
                cur += ch;
        }
        if (!cur.empty()) {
            try {
                exclude_ids.push_back(std::stoull(cur));
            } catch (...) {
            }
        }
    }

    auto clips = service_.get_related_clips(seed_id, req.user_id, limit, parse_clip_langs_query(req),
                                            mix_seed, exclude_ids);
    nlohmann::json j = nlohmann::json::array();
    for (auto &c : clips)
        j.push_back(c);
    return {200, {{"clips", j}, {"count", clips.size()}}};
}

server::Response ClipController::handle_search(const server::Request &req) {
    auto it = req.query.find("q");
    if (it == req.query.end())
        return {400, {{"error", "Missing query parameter 'q'"}}};

    uint32_t limit = 20, offset = 0;
    auto lit = req.query.find("limit");
    if (lit != req.query.end())
        limit = static_cast<uint32_t>(std::stoul(lit->second));
    auto oit = req.query.find("offset");
    if (oit != req.query.end())
        offset = static_cast<uint32_t>(std::stoul(oit->second));

    auto clips = service_.search(it->second, limit, offset, req.user_id, parse_clip_langs_query(req));
    nlohmann::json j = nlohmann::json::array();
    for (auto &c : clips)
        j.push_back(c);
    return {200, {{"clips", j}, {"count", clips.size()}}};
}

server::Response ClipController::handle_discussed_clips(const server::Request &req) {
    uint32_t hours = 24;
    auto hit = req.query.find("hours");
    if (hit != req.query.end()) {
        try {
            hours = static_cast<uint32_t>(std::stoul(hit->second));
        } catch (...) {
        }
    }
    uint32_t limit = 12;
    auto lit = req.query.find("limit");
    if (lit != req.query.end()) {
        try {
            limit = static_cast<uint32_t>(std::stoul(lit->second));
        } catch (...) {
        }
    }
    auto clips = service_.get_discussed_clips(hours, limit, req.user_id, parse_clip_langs_query(req));
    nlohmann::json j = nlohmann::json::array();
    for (auto &c : clips)
        j.push_back(c);
    return {200, {{"clips", j}, {"count", clips.size()}, {"hours", hours}}};
}

server::Response ClipController::handle_continue_watching(const server::Request &req) {
    if (req.user_id == 0)
        return {401, {{"error", "Unauthorized"}}};
    uint32_t limit = 16;
    auto lit = req.query.find("limit");
    if (lit != req.query.end()) {
        try {
            limit = static_cast<uint32_t>(std::stoul(lit->second));
        } catch (...) {
        }
    }
    auto clips = service_.get_continue_watching(limit, req.user_id, parse_clip_langs_query(req));
    nlohmann::json j = nlohmann::json::array();
    for (auto &c : clips)
        j.push_back(c);
    return {200, {{"clips", j}, {"count", clips.size()}}};
}

server::Response ClipController::handle_channel_updates(const server::Request &req) {
    if (req.user_id == 0)
        return {401, {{"error", "Unauthorized"}}};
    uint32_t limit = 12;
    auto lit = req.query.find("limit");
    if (lit != req.query.end()) {
        try {
            limit = static_cast<uint32_t>(std::stoul(lit->second));
        } catch (...) {
        }
    }
    auto clips = service_.get_channel_updates_for_viewer(limit, req.user_id, parse_clip_langs_query(req));
    nlohmann::json j = nlohmann::json::array();
    for (auto &c : clips)
        j.push_back(c);
    return {200, {{"clips", j}, {"count", clips.size()}}};
}

server::Response ClipController::handle_random_clip(const server::Request &req) {
    auto clip = service_.get_random_clip(req.user_id, parse_clip_langs_query(req));
    if (!clip)
        return {404, {{"error", "No clips"}}};
    nlohmann::json j = *clip;
    return {200, {{"clip", j}}};
}

server::Response ClipController::handle_view(const server::Request &req) {
    auto it = req.params.find("id");
    if (it == req.params.end())
        return {400, {{"error", "Missing clip id"}}};
    service_.record_view(std::stoull(it->second), req.user_id);
    return {200, {{"ok", true}}};
}

server::Response ClipController::handle_clip_engagement(const server::Request &req) {
    if (req.user_id == 0)
        return {401, {{"error", "Unauthorized"}}};
    auto it = req.params.find("id");
    if (it == req.params.end())
        return {400, {{"error", "Missing clip id"}}};

    const uint64_t clip_id = std::stoull(it->second);
    const float watched_sec = req.body.value("watched_sec", 0.0f);
    const float duration_sec = req.body.value("duration_sec", 0.0f);
    const bool session_final = req.body.value("session_final", false);
    const std::string session_id = req.body.value("session_id", std::string{});

    service_.record_clip_engagement(clip_id, req.user_id, watched_sec, duration_sec, session_final,
                                    session_id);
    return {200, {{"ok", true}}};
}

server::Response ClipController::handle_clip_feedback(const server::Request &req) {
    if (req.user_id == 0)
        return {401, {{"error", "Unauthorized"}}};
    auto it = req.params.find("id");
    if (it == req.params.end())
        return {400, {{"error", "Missing clip id"}}};

    const std::string action = req.body.value("action", std::string{});
    if (action.empty())
        return {400, {{"error", "Missing action"}}};

    const uint64_t clip_id = std::stoull(it->second);
    if (!service_.record_clip_feedback(clip_id, req.user_id, action))
        return {400, {{"error", "Invalid action or clip"}}};
    return {200, {{"ok", true}}};
}

server::Response ClipController::handle_home_sections(const server::Request &req) {
    uint32_t per_section = 12;
    auto it = req.query.find("limit");
    if (it != req.query.end()) {
        try {
            per_section = static_cast<uint32_t>(std::stoul(it->second));
        } catch (...) {
        }
    }
    if (per_section < 4)
        per_section = 4;
    if (per_section > 24)
        per_section = 24;

    std::vector<std::string> tracked_ids;
    if (req.user_id != 0) {
        auto user = auth_.get_user(req.user_id);
        if (user && !user->twitch_access_token.empty() && !user->twitch_id.empty()) {
            auto followed =
                twitch_.get_followed_channels(auth_.user_access_token(user->id), user->twitch_id, 100);
            tracked_ids.reserve(followed.size());
            for (const auto &ch : followed) {
                auto id = ch.value("broadcaster_id", std::string{});
                if (!id.empty())
                    tracked_ids.push_back(id);
            }
        }
    }
    std::string session_id;
    auto qs = req.query.find("session_id");
    if (qs != req.query.end())
        session_id = qs->second;

    auto sections = service_.get_home_sections(req.user_id, per_section, tracked_ids, session_id,
                                               parse_clip_langs_query(req));
    return {200, sections};
}

server::Response ClipController::handle_home_feed(const server::Request &req) {
    uint32_t limit = 24;
    auto lit = req.query.find("limit");
    if (lit != req.query.end()) {
        try {
            limit = static_cast<uint32_t>(std::stoul(lit->second));
        } catch (...) {
        }
    }

    auto value = [&](const char *key) {
        auto it=req.query.find(key);
        return it==req.query.end()?std::string{}:it->second;
    };
    auto surface=value("surface");
    if(surface.empty()) surface="home";
    return {200,service_.get_home_feed(req.user_id,limit,value("cursor"),value("session_id"),
                                      parse_clip_langs_query(req),surface)};
}

server::Response ClipController::handle_admin_analytics_summary(const server::Request &req) {
    if (req.user_id == 0)
        return {401, {{"error", "Unauthorized"}}};
    auto user = auth_.get_user(req.user_id);
    if (!user || user->role != models::UserRole::Admin)
        return {403, {{"error", "Forbidden"}}};
    return {200, service_.get_analytics_summary_24h()};
}

server::Response ClipController::handle_like(const server::Request &req) {
    if (req.user_id == 0)
        return {401, {{"error", "Unauthorized"}}};

    auto it = req.params.find("id");
    if (it == req.params.end())
        return {400, {{"error", "Missing clip id"}}};

    const uint64_t clip_id = std::stoull(it->second);
    bool is_like = req.body.value("is_like", true);
    if (!service_.toggle_like(clip_id, req.user_id, is_like))
        return {400, {{"error", "Like failed"}}};
    auto j = service_.get_clip_json_for_viewer(clip_id, req.user_id);
    if (!j)
        return {404, {{"error", "Clip not found"}}};
    return {200, *j};
}

server::Response ClipController::handle_report_clip(const server::Request &req) {
    if (req.user_id == 0)
        return {401, {{"error", "Unauthorized"}}};
    auto it = req.params.find("id");
    if (it == req.params.end())
        return {400, {{"error", "Missing clip id"}}};
    std::string reason = req.body.value("reason", std::string{});
    if (reason.size() > 500)
        reason = utils::utf8_prefix(reason, 500);
    if (reason.empty())
        return {400, {{"error", "reason required"}}};
    if (!service_.report_clip(req.user_id, std::stoull(it->second), reason))
        return {404, {{"error", "Clip not found"}}};
    return {200, {{"ok", true}}};
}

server::Response ClipController::handle_games(const server::Request &req) {
    const bool personalized = req.query.contains("personalized") && req.query.at("personalized") == "1";
    auto games = personalized ? (req.user_id ? service_.get_games(req.user_id)
                                           : std::vector<nlohmann::json>{})
                              : service_.get_games();
    return {200, {{"games", games}}};
}

server::Response ClipController::handle_live_streams(const server::Request &req) {
    std::string game_twitch_id;
    std::string sort = "viewers";
    std::string language;
    int limit = 12;

    auto it = req.query.find("game_twitch_id");
    if (it != req.query.end())
        game_twitch_id = it->second;
    it = req.query.find("limit");
    if (it != req.query.end()) {
        try {
            limit = std::stoi(it->second);
        } catch (...) {
        }
    }
    it = req.query.find("sort");
    if (it != req.query.end())
        sort = it->second;
    it = req.query.find("language");
    if (it != req.query.end())
        language = it->second;

    auto streams = twitch_.fetch_live_streams(game_twitch_id, limit, sort, language);
    return {200, {{"streams", streams}, {"count", streams.size()}}};
}

server::Response ClipController::handle_channel_search(const server::Request &req) {
    auto it = req.query.find("q");
    if (it == req.query.end() || it->second.empty())
        return {400, {{"error", "Missing query parameter 'q'"}}};

    int limit = 20;
    auto lit = req.query.find("limit");
    if (lit != req.query.end()) {
        try {
            limit = std::stoi(lit->second);
        } catch (...) {
        }
    }

    auto channels = twitch_.search_channels(it->second, limit);
    return {200, {{"channels", channels}, {"count", channels.size()}}};
}

server::Response ClipController::handle_followed_channels(const server::Request &req) {
    if (req.user_id == 0)
        return {401, {{"error", "Unauthorized"}}};
    auto user = auth_.get_user(req.user_id);
    if (!user || user->twitch_access_token.empty() || user->twitch_id.empty())
        return {200, {{"channels", nlohmann::json::array()}, {"count", 0}}};

    std::string access_token = auth_.user_access_token(user->id);

    auto followed_channels = twitch_.get_followed_channels(access_token, user->twitch_id, 100);
    service_.save_recommendation_twitch_follows(req.user_id,followed_channels);
    auto followed_streams = twitch_.get_followed_streams(access_token, user->twitch_id, 100);
    std::unordered_map<std::string, nlohmann::json> live_by_id;
    live_by_id.reserve(followed_streams.size());
    for (const auto &s : followed_streams) {
        auto id = s.value("user_id", std::string{});
        if (!id.empty())
            live_by_id[id] = s;
    }

    std::vector<std::string> metadata_ids;
    for (const auto &ch : followed_channels)
        metadata_ids.push_back(ch.value("broadcaster_id", std::string{}));
    auto users = twitch_.fetch_users_by_ids(metadata_ids);
    nlohmann::json out = nlohmann::json::array();
    for (const auto &ch : followed_channels) {
        const auto broadcaster_id = ch.value("broadcaster_id", std::string{});
        const auto live_it = live_by_id.find(broadcaster_id);
        const bool is_live = !broadcaster_id.empty() && live_it != live_by_id.end();
        std::string game_name;
        std::string stream_title;
        std::string started_at;
        if (is_live) {
            game_name = live_it->second.value("game_name", std::string{});
            stream_title = live_it->second.value("title", std::string{});
            started_at = live_it->second.value("started_at", std::string{});
        }
        std::string avatar_url;
        if (!broadcaster_id.empty()) {
            auto tuser = users.find(broadcaster_id);
            if (tuser != users.end())
                avatar_url = tuser->second.profile_image_url;
        }

        out.push_back({{"broadcaster_id", broadcaster_id},
                       {"broadcaster_login", ch.value("broadcaster_login", "")},
                       {"broadcaster_name", ch.value("broadcaster_name", "")},
                       {"followed_at", ch.value("followed_at", "")},
                       {"is_live", is_live},
                       {"avatar_url", avatar_url},
                       {"game_name", game_name},
                       {"stream_title", stream_title},
                       {"started_at", started_at}});
    }
    return {200, {{"channels", out}, {"count", out.size()}}};
}

server::Response ClipController::handle_streams_check(const server::Request &req) {
    auto it = req.query.find("ids");
    if (it == req.query.end() || it->second.empty())
        return {200, {{"live", nlohmann::json::object()}}};

    std::vector<std::string> ids;
    std::vector<std::string> logins;
    std::istringstream ss(it->second);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty())
            continue;
        ids.push_back(token);
        logins.push_back(token);
    }

    auto live_set_ids = twitch_.check_live_by_user_ids(ids);
    auto live_set_logins = twitch_.check_live_by_logins(logins);

    nlohmann::json result = nlohmann::json::object();
    for (auto &id : ids) {
        result[id] = live_set_ids.count(id) > 0 || live_set_logins.count(id) > 0;
    }

    return {200, {{"live", result}}};
}

server::Response ClipController::handle_channels_metadata(const server::Request &req) {
    if (req.user_id == 0)
        return {401, {{"error", "Unauthorized"}}};
    auto it = req.query.find("ids");
    if (it == req.query.end() || it->second.empty())
        return {200, {{"channels", nlohmann::json::array()}}};

    auto user = auth_.get_user(req.user_id);
    if (!user || user->twitch_access_token.empty())
        return {200, {{"channels", nlohmann::json::array()}}};

    std::vector<std::string> ids;
    std::istringstream ss(it->second);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (!token.empty())
            ids.push_back(token);
    }

    auto channels = twitch_.get_channels_info(auth_.user_access_token(user->id), ids);
    return {200, {{"channels", channels}, {"count", channels.size()}}};
}

server::Response ClipController::handle_similar_channels(const server::Request &req) {
    if (req.user_id == 0)
        return {200, {{"channels", nlohmann::json::array()}, {"count", 0}}};
    uint32_t limit = 8;
    auto it = req.query.find("limit");
    if (it != req.query.end()) {
        try {
            limit = static_cast<uint32_t>(std::stoul(it->second));
        } catch (...) {
        }
    }
    if (limit < 1)
        limit = 1;
    if (limit > 20)
        limit = 20;
    return {200, service_.get_similar_streamers(req.user_id, limit)};
}

server::Response ClipController::handle_channel_profile(const server::Request &req) {
    auto it = req.query.find("login");
    if (it == req.query.end() || it->second.empty())
        return {400, {{"error", "Missing query parameter 'login'"}}};

    auto profile = twitch_.get_public_channel_profile(it->second);
    std::string uid;
    if (profile.value("found", false) && profile.contains("twitch") && profile["twitch"].is_object() &&
        profile["twitch"].contains("id")) {
        uid = profile["twitch"]["id"].get<std::string>();
    }
    const uint64_t n = service_.count_clips_for_broadcaster(it->second, uid);
    profile["clipstream_clips"] = n;
    if (!uid.empty()) {
        auto site = auth_.site_user_by_twitch_id(uid);
        profile["clipstream_site"] = site ? nlohmann::json(*site) : nlohmann::json(nullptr);
    } else {
        profile["clipstream_site"] = nullptr;
    }
    return {200, profile};
}

} // namespace clipstream::controllers
