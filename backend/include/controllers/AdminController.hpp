#pragma once

#include "repositories/ClipRepository.hpp"
#include "repositories/SyncRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "server/Router.hpp"
#include "services/AuthService.hpp"

namespace clipstream::controllers {

class AdminController {
  public:
    AdminController(services::AuthService& auth, repositories::UserRepository& users,
                    repositories::ClipRepository& clips, repositories::SyncRepository& sync,
                    services::TwitchService& twitch);

    void register_routes(server::Router& router);

  private:
    server::Response handle_dashboard(const server::Request& req);
    server::Response handle_list_users(const server::Request& req);
    server::Response handle_set_role(const server::Request& req);
    server::Response handle_list_promotions(const server::Request& req);
    server::Response handle_create_promotion(const server::Request& req);
    server::Response handle_delete_promotion(const server::Request& req);

    services::AuthService& auth_;
    repositories::UserRepository& users_;
    repositories::ClipRepository& clips_;
    repositories::SyncRepository& sync_;
    services::TwitchService& twitch_;
};

} // namespace clipstream::controllers
