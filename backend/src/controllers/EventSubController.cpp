#include "controllers/EventSubController.hpp"

namespace clipstream::controllers {

EventSubController::EventSubController(services::EventSubService& eventsub_service)
    : eventsub_(eventsub_service) {}

void EventSubController::register_routes(server::Router& router) {
    router.get("/eventsub/status", [this](const server::Request& r) { return handle_status(r); });
    router.get("/eventsub/streams/:broadcaster_id", [this](const server::Request& r) { return handle_stream_state(r); });
    router.get("/eventsub/clips/:broadcaster_id", [this](const server::Request& r) { return handle_latest_clip(r); });
}

server::Response EventSubController::handle_status(const server::Request&) {
    return {200, {{"eventsub", eventsub_.status()}}};
}

server::Response EventSubController::handle_stream_state(const server::Request& req) {
    auto it = req.params.find("broadcaster_id");
    if (it == req.params.end()) return {400, {{"error", "Missing broadcaster_id"}}};
    return {200, eventsub_.stream_state(it->second)};
}

server::Response EventSubController::handle_latest_clip(const server::Request& req) {
    auto it = req.params.find("broadcaster_id");
    if (it == req.params.end()) return {400, {{"error", "Missing broadcaster_id"}}};
    return {200, eventsub_.latest_clip(it->second)};
}

} // namespace clipstream::controllers

