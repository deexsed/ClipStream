#pragma once

#include "server/Router.hpp"
#include "repositories/NotificationRepository.hpp"

namespace clipstream::controllers {

class NotificationController {
public:
    explicit NotificationController(repositories::NotificationRepository& repo);
    void register_routes(server::Router& router);

private:
    server::Response handle_list(const server::Request& req);
    server::Response handle_unread_count(const server::Request& req);
    server::Response handle_mark_read(const server::Request& req);
    server::Response handle_mark_all_read(const server::Request& req);

    repositories::NotificationRepository& repo_;
};

} // namespace clipstream::controllers
