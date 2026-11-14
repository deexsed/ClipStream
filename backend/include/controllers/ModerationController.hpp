#pragma once

#include "repositories/AccountSecurityRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "server/Router.hpp"
#include "services/AuthService.hpp"

namespace clipstream::controllers {

class ModerationController {
  public:
    ModerationController(repositories::AccountSecurityRepository &security, services::CacheService &cache);

    void register_routes(server::Router &router);

  private:
    server::Response handle_restriction(const server::Request &req);

    repositories::AccountSecurityRepository &security_;
    services::CacheService &cache_;
};

} // namespace clipstream::controllers
