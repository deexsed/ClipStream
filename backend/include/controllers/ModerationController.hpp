#pragma once

#include "server/Router.hpp"
#include "services/AuthService.hpp"
#include "repositories/UserRepository.hpp"

namespace clipstream::controllers {

class ModerationController {
public:
    ModerationController(services::AuthService& auth, repositories::UserRepository& users);

    void register_routes(server::Router& router);

private:
    server::Response handle_restriction(const server::Request& req);

    services::AuthService&        auth_;
    repositories::UserRepository& users_;
};

} // namespace clipstream::controllers
