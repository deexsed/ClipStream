#pragma once

#include "repositories/NotificationRepository.hpp"
#include "repositories/ProfileRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "server/Router.hpp"
#include "services/AuthService.hpp"
#include "services/ClipService.hpp"
#include "services/TwitchService.hpp"

namespace clipstream::controllers {

class SocialController {
  public:
    SocialController(services::AuthService &auth, services::ClipService &clips,
                     services::TwitchService &twitch, repositories::UserRepository &users,
                     repositories::NotificationRepository &notifications,
                     repositories::ProfileRepository &profiles);
    void register_routes(server::Router &router);

  private:
    server::Response handle_public_profile(const server::Request &req);
    server::Response handle_friend_request_send(const server::Request &req);
    server::Response handle_friend_request_cancel(const server::Request &req);
    server::Response handle_friend_accept(const server::Request &req);
    server::Response handle_friend_decline(const server::Request &req);
    server::Response handle_friend_remove(const server::Request &req);
    server::Response handle_friends_list(const server::Request &req);
    server::Response handle_friend_requests_incoming(const server::Request &req);
    server::Response handle_friend_requests_outgoing(const server::Request &req);
    server::Response handle_wall_list(const server::Request &req);
    server::Response handle_wall_create(const server::Request &req);
    server::Response handle_user_settings_get(const server::Request &req);
    server::Response handle_user_settings_put(const server::Request &req);
    server::Response handle_friends_feed(const server::Request &req);
    server::Response handle_followers_list(const server::Request &req);
    server::Response handle_following_list(const server::Request &req);
    server::Response handle_user_clips(const server::Request &req);
    server::Response handle_watch_activity(const server::Request &req);
    server::Response handle_user_search(const server::Request &req);

    repositories::ProfileRepository &profiles_;
    services::AuthService &auth_;
    services::ClipService &clips_;
    services::TwitchService &twitch_;
    repositories::UserRepository &users_;
    repositories::NotificationRepository &notifications_;
};

} // namespace clipstream::controllers
