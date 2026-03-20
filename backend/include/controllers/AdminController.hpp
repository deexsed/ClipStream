#pragma once

#include "server/Router.hpp"
#include "services/AuthService.hpp"
#include "repositories/UserRepository.hpp"
#include "repositories/ClipRepository.hpp"

namespace clipstream::controllers {

class AdminController {
public:
    AdminController(services::AuthService& auth, repositories::UserRepository& users,
                    repositories::ClipRepository& clips);

    void register_routes(server::Router& router);

private:
    server::Response handle_dashboard(const server::Request& req);
    server::Response handle_list_users(const server::Request& req);
    server::Response handle_set_role(const server::Request& req);
    server::Response handle_list_promotions(const server::Request& req);
    server::Response handle_create_promotion(const server::Request& req);
    server::Response handle_delete_promotion(const server::Request& req);

    services::AuthService&        auth_;
    repositories::UserRepository& users_;
    repositories::ClipRepository& clips_;
};

} // namespace clipstream::controllers
