#pragma once

#include "server/Router.hpp"
#include "repositories/CommentRepository.hpp"
#include "repositories/ClipRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "repositories/NotificationRepository.hpp"
#include "services/AuthService.hpp"

namespace clipstream::controllers {

class CommentController {
public:
    CommentController(repositories::CommentRepository& repo, services::AuthService& auth,
                      repositories::ClipRepository& clips, repositories::UserRepository& users,
                      repositories::NotificationRepository& notifications);
    void register_routes(server::Router& router);

private:
    server::Response handle_get(const server::Request& req);
    server::Response handle_create(const server::Request& req);
    server::Response handle_update(const server::Request& req);
    server::Response handle_delete(const server::Request& req);
    server::Response handle_vote(const server::Request& req);

    repositories::CommentRepository&      repo_;
    services::AuthService&                  auth_;
    repositories::ClipRepository&          clips_;
    repositories::UserRepository&          users_;
    repositories::NotificationRepository& notifications_;
};

} // namespace clipstream::controllers
