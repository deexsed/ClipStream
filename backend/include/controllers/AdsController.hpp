#pragma once
#include "repositories/AdsRepository.hpp"
#include "server/Router.hpp"
#include "services/AuthService.hpp"
#include <set>

namespace clipstream::controllers {
class AdsController {
public:
    AdsController(services::AuthService &auth, repositories::AdsRepository &ads,
                  const std::string &secret);
    void register_routes(server::Router &router);
private:
    bool admin(const server::Request &req);
    server::Response list(const server::Request &req);
    server::Response find(const server::Request &req);
    server::Response save(const server::Request &req, bool update);
    server::Response remove(const server::Request &req);
    server::Response serve(const server::Request &req);
    server::Response event(const server::Request &req);
    services::AuthService &auth_;
    repositories::AdsRepository &ads_;
    std::string secret_;
    std::set<std::string> hosts_;
};
} // namespace clipstream::controllers
