#pragma once

#include "server/Router.hpp"
#include "services/AuthService.hpp"
#include "services/CacheService.hpp"
#include "services/TwitchService.hpp"

namespace clipstream::controllers {

class AuthController {
public:
    AuthController(services::AuthService& auth_service, services::TwitchService& twitch_service,
                   services::CacheService& cache_service,
                   const std::string& redirect_uri);
    void register_routes(server::Router& router);

private:
    server::Response handle_register(const server::Request& req);
    server::Response handle_login(const server::Request& req);
    server::Response handle_me(const server::Request& req);
    server::Response handle_twitch_url(const server::Request& req);
    server::Response handle_twitch_login(const server::Request& req);
    static std::string generate_state();

    services::AuthService& auth_;
    services::TwitchService& twitch_;
    services::CacheService& cache_;
    std::string redirect_uri_;
};

} // namespace clipstream::controllers
