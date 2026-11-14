#include "services/EventSubService.hpp"
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <openssl/ssl.h>
#include <random>
#include <sstream>

namespace clipstream::services {
namespace beast = boost::beast;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using json = nlohmann::json;
namespace {
struct Session {
    tcp::resolver resolver;
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws;
    asio::steady_timer watchdog;
    beast::flat_buffer buffer;
    std::string target;
    std::string id;
    int keepalive{10};
    bool transfer{};
    bool closed{};
    Session(asio::io_context &io, ssl::context &ctx, std::string path, bool moved)
        : resolver(io), ws(io, ctx), watchdog(io), target(std::move(path)), transfer(moved) {}
    void close() {
        if (closed)
            return;
        closed = true;
        resolver.cancel();
        watchdog.cancel();
        boost::system::error_code ec;
        beast::get_lowest_layer(ws).socket().cancel(ec);
        beast::get_lowest_layer(ws).socket().close(ec);
    }
};
} // namespace
EventSubService::EventSubService(TwitchService &twitch, CacheService &cache, AuthService &auth,
                                 std::vector<std::string> ids, std::function<std::string()> provider)
    : twitch_(twitch), cache_(cache), auth_(auth), broadcaster_ids_(std::move(ids)),
      token_provider_(std::move(provider)) {
    enabled_ = !broadcaster_ids_.empty() && static_cast<bool>(token_provider_);
}
EventSubService::~EventSubService() { stop(); }
void EventSubService::start() {
    if (!enabled_ || running_.exchange(true))
        return;
    worker_ = std::thread([this] { run(); });
}
void EventSubService::stop() {
    running_ = false;
    wake_.notify_all();
    {
        std::lock_guard lock(cancel_mx_);
        if (cancel_)
            cancel_();
    }
    if (worker_.joinable())
        worker_.join();
}
void EventSubService::publish_status() const {
    if (running_)
        cache_.set("eventsub:worker:status", status().dump(), 45);
}
json EventSubService::status() const {
    if (!running_) {
        if (auto raw = cache_.get("eventsub:worker:status")) {
            auto shared = json::parse(*raw, nullptr, false);
            if (shared.is_object())
                return shared;
        }
    }
    std::lock_guard lock(state_mx_);
    return {{"enabled", enabled_},
            {"connected", connected_},
            {"subscribed", subscribed_},
            {"session_id", session_id_},
            {"last_message_at", last_message_at_},
            {"last_error", last_error_},
            {"broadcasters", broadcaster_ids_},
            {"subscriptions", subscriptions_},
            {"clip_discovery", "helix_polling"},
            {"oauth_revocation", "hourly_token_validation"}};
}
json EventSubService::stream_state(const std::string &id) const {
    auto v = cache_.get("eventsub:stream:" + id);
    if (v) {
        auto j = json::parse(*v, nullptr, false);
        if (j.is_object())
            return j;
    }
    return {{"broadcaster_id", id}, {"status", "unknown"}};
}
json EventSubService::latest_clip(const std::string &id) const {
    auto v = cache_.get("eventsub:clip_latest:" + id);
    if (v) {
        auto j = json::parse(*v, nullptr, false);
        if (j.is_object())
            return j;
    }
    return {{"broadcaster_id", id}, {"clip", nullptr}};
}
void EventSubService::run() {
    while (running_) {
        std::atomic<int> retry_seconds{3};
        const auto lease =
            std::to_string(std::random_device{}()) + ":" + std::to_string(std::random_device{}());
        bool acquired = false;
        try {
            acquired = cache_.try_set_nx("clipstream:eventsub:lease", lease, 60);
            if (acquired) {
                // Resolve/refresh before Welcome: Twitch requires the first subscription promptly.
                const auto token = token_provider_();
                if (token.empty())
                    throw TwitchError(401, "EventSub requires a user access token");
                {
                    std::lock_guard lock(state_mx_);
                    session_token_ = token;
                }
                if (!cache_.compare_expire("clipstream:eventsub:lease", lease, 60))
                    throw TwitchError(503, "EventSub lease lost during token refresh", 3);
                auto io = std::make_shared<asio::io_context>();
                ssl::context ctx(ssl::context::tls_client);
                ctx.set_default_verify_paths();
                ctx.set_verify_mode(ssl::verify_peer);
                std::vector<std::shared_ptr<Session>> sessions;
                std::shared_ptr<Session> current;
                std::jthread subscribe_worker;
                asio::steady_timer renewal(*io);
                std::function<void()> renew;
                renew = [&] {
                    renewal.expires_after(std::chrono::seconds(15));
                    renewal.async_wait([&](auto ec) {
                        if (ec)
                            return;
                        bool held = false;
                        try {
                            held = cache_.compare_expire("clipstream:eventsub:lease", lease, 60);
                        } catch (...) {
                        }
                        if (!held || !running_) {
                            for (auto &s : sessions)
                                s->close();
                            return;
                        }
                        publish_status();
                        renew();
                    });
                };
                std::function<void(std::shared_ptr<Session>)> read;
                std::function<void(std::string, bool)> connect;
                auto fail = [&](std::shared_ptr<Session> s, const std::string &error) {
                    s->close();
                    if (s == current || !current) {
                        for (auto &other : sessions)
                            other->close();
                        renewal.cancel();
                        std::lock_guard lock(state_mx_);
                        connected_ = false;
                        subscribed_ = false;
                        session_id_.clear();
                        last_error_ = error;
                    }
                };
                auto arm = [&](std::shared_ptr<Session> s) {
                    s->watchdog.expires_after(std::chrono::seconds(s->keepalive + 2));
                    s->watchdog.async_wait([&, s](auto ec) {
                        if (!ec)
                            fail(s, "EventSub keepalive deadline exceeded");
                    });
                };
                read = [&](std::shared_ptr<Session> s) {
                    s->ws.async_read(s->buffer, [&, s](auto ec, std::size_t) {
                        if (ec) {
                            if (!s->closed)
                                fail(s, "EventSub socket closed");
                            return;
                        }
                        try {
                            auto msg =
                                json::parse(beast::buffers_to_string(s->buffer.data()), nullptr, false);
                            s->buffer.consume(s->buffer.size());
                            if (!msg.is_object()) {
                                fail(s, "Invalid EventSub JSON");
                                return;
                            }
                            arm(s);
                            const auto type = msg.value("metadata", json::object()).value("message_type", "");
                            if (type == "session_welcome") {
                                auto data = msg.at("payload").at("session");
                                s->id = data.value("id", "");
                                s->keepalive =
                                    data.contains("keepalive_timeout_seconds") &&
                                            data["keepalive_timeout_seconds"].is_number_integer()
                                        ? std::clamp(data["keepalive_timeout_seconds"].get<int>(), 10, 600)
                                        : 10;
                                arm(s);
                                if (s->id.empty())
                                    throw std::runtime_error("Empty EventSub session");
                                auto old = current;
                                current = s;
                                {
                                    std::lock_guard lock(state_mx_);
                                    session_id_ = s->id;
                                    connected_ = true;
                                    last_error_.clear();
                                    last_message_at_ = now_iso();
                                }
                                // A reconnect keeps the old socket receiving until the replacement Welcome.
                                if (old && old != s)
                                    old->close();
                                std::erase_if(sessions,
                                              [&](const auto &item) { return item != s && item->closed; });
                                publish_status();
                                if (!s->transfer) {
                                    if (subscribe_worker.joinable()) {
                                        subscribe_worker.request_stop();
                                        subscribe_worker.join();
                                    }
                                    subscribe_worker = std::jthread([this, id = s->id, io,
                                                                     &retry_seconds](std::stop_token stop) {
                                        try {
                                            subscribe_all(id, stop);
                                        } catch (const TwitchError &error) {
                                            retry_seconds = std::clamp(
                                                error.retry_after > 0
                                                    ? error.retry_after
                                                    : (error.status == 401 || error.status == 403 ? 60 : 5),
                                                3, 300);
                                            {
                                                std::lock_guard lock(state_mx_);
                                                last_error_ =
                                                    "EventSub subscription authorization or upstream failure";
                                                subscribed_ = false;
                                            }
                                            // Closing the session discards partial subscriptions; a bounded
                                            // retry rebuilds the complete set.
                                            io->stop();
                                        } catch (const std::exception &) {
                                            retry_seconds = 5;
                                            {
                                                std::lock_guard lock(state_mx_);
                                                last_error_ = "EventSub subscription failure";
                                                subscribed_ = false;
                                            }
                                            io->stop();
                                        }
                                    });
                                }
                            } else if (type == "session_reconnect") {
                                const auto url = msg.at("payload").at("session").value("reconnect_url", "");
                                constexpr std::string_view prefix = "wss://eventsub.wss.twitch.tv/";
                                if (!url.starts_with(prefix))
                                    throw std::runtime_error("Untrusted EventSub reconnect URL");
                                connect(url.substr(prefix.size() - 1), true);
                            } else
                                handle_message(msg);
                        } catch (const std::exception &) {
                            fail(s, "EventSub message handling failed");
                            return;
                        }
                        if (!s->closed && running_)
                            read(s);
                    });
                };
                connect = [&](std::string target, bool transfer) {
                    if (transfer && std::any_of(sessions.begin(), sessions.end(), [&](const auto &item) {
                            return item != current && item->transfer && !item->closed;
                        }))
                        return;
                    auto s = std::make_shared<Session>(*io, ctx, std::move(target), transfer);
                    sessions.push_back(s);
                    s->ws.next_layer().set_verify_callback(
                        ssl::host_name_verification("eventsub.wss.twitch.tv"));
                    if (!SSL_set_tlsext_host_name(s->ws.next_layer().native_handle(),
                                                  "eventsub.wss.twitch.tv")) {
                        fail(s, "TLS SNI setup failed");
                        return;
                    }
                    s->ws.read_message_max(1024 * 1024);
                    s->keepalive = 30;
                    arm(s);
                    s->resolver.async_resolve(
                        "eventsub.wss.twitch.tv", "443", [&, s](auto ec, auto endpoints) {
                            if (ec) {
                                fail(s, "EventSub DNS failed");
                                return;
                            }
                            beast::get_lowest_layer(s->ws).async_connect(endpoints, [&, s](auto ec, auto) {
                                if (ec) {
                                    fail(s, "EventSub connect failed");
                                    return;
                                }
                                s->ws.next_layer().async_handshake(ssl::stream_base::client, [&, s](auto ec) {
                                    if (ec) {
                                        fail(s, "EventSub certificate or TLS failure");
                                        return;
                                    }
                                    s->ws.async_handshake("eventsub.wss.twitch.tv", s->target,
                                                          [&, s](auto ec) {
                                                              if (ec) {
                                                                  fail(s, "EventSub handshake failed");
                                                                  return;
                                                              }
                                                              read(s);
                                                          });
                                });
                            });
                        });
                };
                {
                    std::lock_guard lock(cancel_mx_);
                    cancel_ = [io] { io->stop(); };
                }
                renew();
                connect("/ws", false);
                io->run();
                for (auto &session : sessions)
                    session->close();
                renewal.cancel();
                {
                    std::lock_guard lock(cancel_mx_);
                    cancel_ = {};
                }
                if (subscribe_worker.joinable()) {
                    subscribe_worker.request_stop();
                    subscribe_worker.join();
                }
                {
                    std::lock_guard lock(state_mx_);
                    connected_ = false;
                    subscribed_ = false;
                    session_id_.clear();
                }
            }
        } catch (const std::exception &error) {
            if (const auto *upstream = dynamic_cast<const TwitchError *>(&error))
                retry_seconds = std::clamp(
                    upstream->retry_after > 0 ? upstream->retry_after
                                              : (upstream->status == 401 || upstream->status == 403 ? 60 : 5),
                    3, 300);
            std::lock_guard lock(state_mx_);
            last_error_ = "EventSub unavailable; retrying";
            connected_ = false;
            subscribed_ = false;
        }
        {
            std::lock_guard lock(cancel_mx_);
            cancel_ = {};
        }
        if (acquired)
            try {
                if (cache_.compare_expire("clipstream:eventsub:lease", lease, 60))
                    cache_.set("eventsub:worker:status",
                               json({{"enabled", true}, {"connected", false}, {"subscribed", false}}).dump(),
                               45);
                cache_.compare_delete("clipstream:eventsub:lease", lease);
            } catch (...) {
            }
        std::unique_lock lock(wake_mx_);
        wake_.wait_for(lock, std::chrono::seconds(retry_seconds.load()), [this] { return !running_; });
    }
}
void EventSubService::subscribe_all(const std::string &session, std::stop_token stop) {
    {
        std::lock_guard lock(state_mx_);
        subscriptions_ = json::array();
        subscribed_ = false;
    }
    for (const auto &id : broadcaster_ids_) {
        if (!running_ || stop.stop_requested())
            return;
        subscribe_type("stream.online", "1", id, session);
        subscribe_type("stream.offline", "1", id, session);
        subscribe_type("channel.update", "2", id, session);
    }
    std::lock_guard lock(state_mx_);
    if (session_id_ == session)
        subscribed_ = true;
}
void EventSubService::subscribe_type(const std::string &type, const std::string &version,
                                     const std::string &id, const std::string &session) {
    subscribe_type_with_condition(type, version, {{"broadcaster_user_id", id}}, session);
}
void EventSubService::subscribe_type_with_condition(const std::string &type, const std::string &version,
                                                    const json &condition, const std::string &session) {
    if (!running_)
        return;
    std::string token;
    {
        std::lock_guard lock(state_mx_);
        token = session_token_;
    }
    if (token.empty())
        throw TwitchError(401, "EventSub requires a user access token");
    auto result =
        twitch_.helix_post_user_json("/helix/eventsub/subscriptions",
                                     {{"type", type},
                                      {"version", version},
                                      {"condition", condition},
                                      {"transport", {{"method", "websocket"}, {"session_id", session}}}},
                                     token);
    if (!result.contains("data") || result["data"].empty())
        throw TwitchError(502, "EventSub subscription was not accepted");
    std::lock_guard lock(state_mx_);
    if (session_id_ != session)
        return;
    for (const auto &sub : result.at("data"))
        subscriptions_.push_back(
            {{"id", sub.value("id", "")}, {"type", type}, {"status", sub.value("status", "")}});
}
void EventSubService::handle_message(const json &msg) {
    auto type = msg.value("metadata", json::object()).value("message_type", "");
    auto payload = msg.value("payload", json::object());
    {
        std::lock_guard lock(state_mx_);
        last_message_at_ = now_iso();
    }
    if (type == "revocation") {
        auto sub = payload.value("subscription", json::object());
        std::lock_guard lock(state_mx_);
        subscribed_ = false;
        last_error_ = "EventSub subscription revoked";
        for (auto &item : subscriptions_)
            if (item.value("id", "") == sub.value("id", ""))
                item["status"] = sub.value("status", "revoked");
        return;
    }
    if (type != "notification")
        return;
    auto event = payload.value("event", json::object());
    const auto id = event.value("broadcaster_user_id", "");
    if (id.empty())
        return;
    const auto topic = payload.value("subscription", json::object()).value("type", "");
    if (topic == "stream.online" || topic == "stream.offline")
        cache_.set("eventsub:stream:" + id,
                   json({{"broadcaster_id", id},
                         {"status", topic == "stream.online" ? "online" : "offline"},
                         {"updated_at", now_iso()}})
                       .dump(),
                   3600);
    if (topic == "channel.update")
        cache_.set("eventsub:channel:" + id, event.dump(), 3600);
    cache_.invalidate_pattern("twitch:streams:*");
    cache_.invalidate_pattern("twitch:pub_profile:*");
    cache_.invalidate_pattern("twitch:live_check*");
    cache_.invalidate_pattern("twitch:helix:v3:*");
}
std::string EventSubService::now_iso() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
} // namespace clipstream::services
