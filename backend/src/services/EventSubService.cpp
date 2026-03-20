#include "services/EventSubService.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <openssl/ssl.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace clipstream::services {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

EventSubService::EventSubService(TwitchService& twitch, CacheService& cache, AuthService& auth,
                                 std::vector<std::string> broadcaster_ids)
    : twitch_(twitch), cache_(cache), auth_(auth), broadcaster_ids_(std::move(broadcaster_ids)) {}

EventSubService::~EventSubService() { stop(); }

void EventSubService::start() {
    if (running_) return;
    running_ = true;
    worker_ = std::thread([this] { run(); });
}

void EventSubService::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

nlohmann::json EventSubService::status() const {
    std::lock_guard<std::mutex> lock(state_mx_);
    return {
        {"enabled", true},
        {"connected", connected_},
        {"session_id", session_id_},
        {"last_message_at", last_message_at_},
        {"last_error", last_error_},
        {"broadcasters", broadcaster_ids_}
    };
}

nlohmann::json EventSubService::stream_state(const std::string& broadcaster_id) const {
    auto v = cache_.get("eventsub:stream:" + broadcaster_id);
    if (!v) return {{"broadcaster_id", broadcaster_id}, {"status", "unknown"}};
    auto j = json::parse(*v, nullptr, false);
    if (j.is_discarded()) return {{"broadcaster_id", broadcaster_id}, {"status", "unknown"}};
    return j;
}

nlohmann::json EventSubService::latest_clip(const std::string& broadcaster_id) const {
    auto v = cache_.get("eventsub:clip_latest:" + broadcaster_id);
    if (!v) return {{"broadcaster_id", broadcaster_id}, {"clip", nullptr}};
    auto j = json::parse(*v, nullptr, false);
    if (j.is_discarded()) return {{"broadcaster_id", broadcaster_id}, {"clip", nullptr}};
    return j;
}

void EventSubService::run() {
    while (running_) {
        try {
            asio::io_context ioc;
            ssl::context ctx(ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();
            ctx.set_verify_mode(ssl::verify_none);

            tcp::resolver resolver(ioc);
            websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws(ioc, ctx);
            ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

            const std::string host = "eventsub.wss.twitch.tv";
            const std::string target = "/ws";

            SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str());
            auto const results = resolver.resolve(host, "443");
            beast::get_lowest_layer(ws).connect(results);
            ws.next_layer().handshake(ssl::stream_base::client);
            ws.handshake(host, target);

            {
                std::lock_guard<std::mutex> lock(state_mx_);
                connected_ = true;
                last_error_.clear();
            }

            while (running_) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                auto msg = beast::buffers_to_string(buffer.data());
                auto j = json::parse(msg, nullptr, false);
                if (j.is_discarded()) continue;
                handle_message(j);
            }
        } catch (const std::exception& e) {
            {
                std::lock_guard<std::mutex> lock(state_mx_);
                connected_ = false;
                session_id_.clear();
                last_error_ = e.what();
            }
            std::cerr << "[EventSub] Reconnect after error: " << e.what() << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
}

void EventSubService::subscribe_all(const std::string& session_id) {
    for (const auto& id : broadcaster_ids_) {
        if (id.empty()) continue;
        subscribe_type("stream.online", "1", id, session_id);
        subscribe_type("stream.offline", "1", id, session_id);
        subscribe_type("channel.clip.create", "1", id, session_id);
        subscribe_type("channel.update", "2", id, session_id);
    }
    subscribe_type_with_condition("user.authorization.revoke", "1",
                                  {{"client_id", twitch_.client_id()}}, session_id);
}

void EventSubService::subscribe_type(const std::string& type, const std::string& version,
                                     const std::string& broadcaster_id, const std::string& session_id) {
    subscribe_type_with_condition(type, version, {{"broadcaster_user_id", broadcaster_id}}, session_id);
}

void EventSubService::subscribe_type_with_condition(const std::string& type, const std::string& version,
                                                    const nlohmann::json& condition, const std::string& session_id) {
    json body = {
        {"type", type},
        {"version", version},
        {"condition", condition},
        {"transport", {{"method", "websocket"}, {"session_id", session_id}}}
    };
    auto res = twitch_.helix_post_json("/helix/eventsub/subscriptions", body);
    if (res.is_discarded() || (res.contains("error") && !res["error"].is_null())) {
        std::cerr << "[EventSub] Subscribe failed type=" << type
                  << " response=" << (res.is_discarded() ? std::string("invalid_json") : res.dump().substr(0, 200)) << "\n";
        return;
    }
    std::cout << "[EventSub] Subscribed " << type << "\n";
}

void EventSubService::handle_message(const nlohmann::json& msg) {
    const auto metadata = msg.value("metadata", json::object());
    const auto payload = msg.value("payload", json::object());
    const auto message_type = metadata.value("message_type", std::string{});

    {
        std::lock_guard<std::mutex> lock(state_mx_);
        last_message_at_ = now_iso();
    }

    if (message_type == "session_welcome") {
        auto session = payload.value("session", json::object());
        auto session_id = session.value("id", std::string{});
        {
            std::lock_guard<std::mutex> lock(state_mx_);
            session_id_ = session_id;
        }
        if (!session_id.empty()) subscribe_all(session_id);
        return;
    }

    if (message_type == "session_keepalive") return;
    if (message_type == "session_reconnect") {
        throw std::runtime_error("EventSub requested reconnect");
    }

    if (message_type == "notification") {
        auto sub = payload.value("subscription", json::object());
        auto event = payload.value("event", json::object());
        const auto type = sub.value("type", std::string{});
        const auto broadcaster_id = event.value("broadcaster_user_id", std::string{});

        if (type == "stream.online") {
            if (broadcaster_id.empty()) return;
            json state = {
                {"broadcaster_id", broadcaster_id},
                {"status", "online"},
                {"started_at", event.value("started_at", "")},
                {"updated_at", now_iso()}
            };
            cache_.set("eventsub:stream:" + broadcaster_id, state.dump(), 12 * 3600);
            cache_.invalidate_pattern("twitch:streams:*");
        } else if (type == "stream.offline") {
            if (broadcaster_id.empty()) return;
            json state = {
                {"broadcaster_id", broadcaster_id},
                {"status", "offline"},
                {"updated_at", now_iso()}
            };
            cache_.set("eventsub:stream:" + broadcaster_id, state.dump(), 12 * 3600);
            cache_.invalidate_pattern("twitch:streams:*");
        } else if (type == "channel.clip.create") {
            if (broadcaster_id.empty()) return;
            json clip = {
                {"broadcaster_id", broadcaster_id},
                {"id", event.value("id", "")},
                {"url", event.value("url", "")},
                {"title", event.value("title", "")},
                {"created_at", event.value("created_at", "")},
                {"updated_at", now_iso()}
            };
            cache_.set("eventsub:clip_latest:" + broadcaster_id, clip.dump(), 24 * 3600);
            cache_.invalidate_pattern("twitch:clips:*");
        } else if (type == "channel.update") {
            if (broadcaster_id.empty()) return;
            json ch = {
                {"broadcaster_id", broadcaster_id},
                {"title", event.value("title", "")},
                {"language", event.value("language", "")},
                {"category_id", event.value("category_id", "")},
                {"category_name", event.value("category_name", "")},
                {"content_classification_labels", event.value("content_classification_labels", nlohmann::json::array())},
                {"updated_at", now_iso()}
            };
            cache_.set("eventsub:channel:" + broadcaster_id, ch.dump(), 24 * 3600);
            cache_.invalidate_pattern("twitch:streams:*");
        } else if (type == "user.authorization.revoke") {
            auto uid = event.value("user_id", std::string{});
            if (!uid.empty()) {
                auth_.clear_twitch_oauth_by_twitch_id(uid);
                cache_.invalidate_pattern("oauth:*");
                std::cout << "[EventSub] OAuth revoked for twitch user " << uid << "\n";
            }
        }
    }
}

std::string EventSubService::now_iso() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

} // namespace clipstream::services

