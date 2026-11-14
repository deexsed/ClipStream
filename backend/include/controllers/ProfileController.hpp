#pragma once
#include "repositories/ProfileRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "server/Router.hpp"
#include "services/ClipService.hpp"
#include "services/TwitchService.hpp"

namespace clipstream::controllers {
class ProfileController {
  public:
    ProfileController(repositories::ProfileRepository &profiles, repositories::UserRepository &users,
                      services::TwitchService &twitch, services::ClipService &clips)
        : profiles_(profiles), users_(users), twitch_(twitch), clips_(clips) {}
    void register_routes(server::Router &router);

  private:
    server::Response twitch_profile(const server::Request &req, bool by_login);
    server::Response user_profile(const server::Request &req);
    server::Response keyed_profile(const server::Request &req);
    server::Response follow(const server::Request &req);
    server::Response list(const server::Request &req, bool followers, bool mine);
    server::Response following_feed(const server::Request &req);
    server::Response profile_clips(const server::Request &req);
    server::Response settings(const server::Request &req);
    server::Response wall_mutation(const server::Request &req);
    nlohmann::json present(const repositories::ProfileEntity &entity, uint64_t viewer,
                           nlohmann::json twitch = nullptr);
    repositories::ProfileRepository &profiles_;
    repositories::UserRepository &users_;
    services::TwitchService &twitch_;
    services::ClipService &clips_;
};
} // namespace clipstream::controllers
