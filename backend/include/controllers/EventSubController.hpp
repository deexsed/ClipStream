#pragma once

#include "server/Router.hpp"
#include "services/EventSubService.hpp"

namespace clipstream::controllers {

class EventSubController {
public:
    explicit EventSubController(services::EventSubService& eventsub_service);
    void register_routes(server::Router& router);

private:
    server::Response handle_status(const server::Request& req);
    server::Response handle_stream_state(const server::Request& req);
    server::Response handle_latest_clip(const server::Request& req);

    services::EventSubService& eventsub_;
};

} // namespace clipstream::controllers

