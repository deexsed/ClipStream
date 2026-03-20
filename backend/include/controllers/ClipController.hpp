#pragma once

#include "server/Router.hpp"
#include "services/ClipService.hpp"
#include "services/TwitchService.hpp"
#include "services/AuthService.hpp"

namespace clipstream::controllers {

class ClipController {
public:
    ClipController(services::ClipService& clip_service, services::TwitchService& twitch_service,
                   services::AuthService& auth_service);
    void register_routes(server::Router& router);

private:
    server::Response handle_get_feed(const server::Request& req);
    server::Response handle_get_clip(const server::Request& req);
    server::Response handle_clip_trend_ranks(const server::Request& req);
    server::Response handle_related_clips(const server::Request& req);
    server::Response handle_search(const server::Request& req);
    server::Response handle_view(const server::Request& req);
    server::Response handle_clip_engagement(const server::Request& req);
    server::Response handle_clip_feedback(const server::Request& req);
    server::Response handle_like(const server::Request& req);
    server::Response handle_report_clip(const server::Request& req);
    server::Response handle_games(const server::Request& req);
    server::Response handle_live_streams(const server::Request& req);
    server::Response handle_channel_search(const server::Request& req);
    server::Response handle_followed_channels(const server::Request& req);
    server::Response handle_streams_check(const server::Request& req);
    server::Response handle_channels_metadata(const server::Request& req);
    server::Response handle_home_sections(const server::Request& req);
    server::Response handle_home_feed(const server::Request& req);
    server::Response handle_admin_analytics_summary(const server::Request& req);
    server::Response handle_similar_channels(const server::Request& req);
    server::Response handle_channel_profile(const server::Request& req);

    services::ClipService& service_;
    services::TwitchService& twitch_;
    services::AuthService& auth_;
};

} // namespace clipstream::controllers
