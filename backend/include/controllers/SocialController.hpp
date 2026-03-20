#pragma once

#include "server/Router.hpp"
#include "services/AuthService.hpp"
#include "services/ClipService.hpp"
#include "repositories/UserRepository.hpp"

namespace clipstream::controllers {

class SocialController {
public:
    SocialController(services::AuthService& auth, services::ClipService& clips, repositories::UserRepository& users);
    void register_routes(server::Router& router);

private:
    server::Response handle_public_profile(const server::Request& req);
    server::Response handle_follow(const server::Request& req);
    server::Response handle_unfollow(const server::Request& req);
    server::Response handle_user_settings_get(const server::Request& req);
    server::Response handle_user_settings_put(const server::Request& req);
    server::Response handle_friends_feed(const server::Request& req);
    server::Response handle_followers_list(const server::Request& req);
    server::Response handle_following_list(const server::Request& req);
    server::Response handle_user_clips(const server::Request& req);
    server::Response handle_watch_activity(const server::Request& req);
    server::Response handle_user_search(const server::Request& req);

    services::AuthService&         auth_;
    services::ClipService&         clips_;
    repositories::UserRepository& users_;
};

} // namespace clipstream::controllers
