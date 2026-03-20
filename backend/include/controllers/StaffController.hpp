#pragma once

#include "server/Router.hpp"
#include "services/AuthService.hpp"
#include "services/CacheService.hpp"
#include "repositories/ClipRepository.hpp"

namespace clipstream::controllers {

class StaffController {
public:
    StaffController(services::AuthService& auth, services::CacheService& cache, repositories::ClipRepository& clip_repo);

    void register_routes(server::Router& router);

private:
    server::Response handle_bootstrap(const server::Request& req);
    server::Response handle_diagnostics(const server::Request& req);
    server::Response handle_content_reports(const server::Request& req);

    services::AuthService&         auth_;
    services::CacheService&        cache_;
    repositories::ClipRepository&   clip_repo_;
};

} // namespace clipstream::controllers
