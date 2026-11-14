#include "services/TwitchService.hpp"
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <memory>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <random>
#include <set>
#include <sstream>

namespace clipstream::services {
namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using json = nlohmann::json;
namespace {
std::string digest(const std::string &value) {
    unsigned char bytes[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(value.data()), value.size(), bytes);
    std::ostringstream out;
    for (auto b : bytes)
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    return out.str();
}
struct Flight {
    CacheService *cache{};
    std::string key;
    std::string token;
    Flight(CacheService *c, const std::string &k)
        : cache(c), key(k), token(std::to_string(std::random_device{}())) {
        if (cache && !cache->try_set_nx(key, token, 120))
            throw TwitchError(503, "Twitch request already in progress", 1);
    }
    ~Flight() {
        if (cache)
            try {
                cache->compare_delete(key, token);
            } catch (...) {
            }
    }
};
std::string encode(const std::string &value) {
    std::string out;
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
json response_json(const std::string &raw, bool data = true) {
    auto j = json::parse(raw, nullptr, false);
    if (!j.is_object() || (data && (!j.contains("data") || !j["data"].is_array())))
        throw TwitchError(502, "Twitch returned an invalid response");
    return j;
}
std::string str(const json &j, const char *name) {
    return j.contains(name) && j[name].is_string() ? j[name].get<std::string>() : std::string{};
}
TwitchUser user_from(const json &u) {
    return {str(u, "id"), str(u, "login"), str(u, "display_name"), str(u, "email"),
            str(u, "profile_image_url")};
}
OAuthTokenSet token_from(const json &j) {
    OAuthTokenSet ts{str(j, "access_token"), str(j, "refresh_token"), j.value("expires_in", 0), {}};
    if (j.contains("scope") && j["scope"].is_array())
        for (const auto &scope : j["scope"]) {
            if (!scope.is_string())
                continue;
            if (!ts.scope.empty())
                ts.scope += ' ';
            ts.scope += scope.get<std::string>();
        }
    if (ts.access_token.empty() || ts.expires_in <= 0)
        throw TwitchError(502, "Twitch returned an invalid token response");
    return ts;
}
} // namespace
TwitchService::TwitchService(const std::string &client_id, const std::string &client_secret,
                             CacheService *cache, HttpTransport transport)
    : client_id_(client_id), client_secret_(client_secret), cache_(cache), transport_(std::move(transport)) {}

// Every stage, including DNS and TLS, shares one bounded asynchronous deadline.
// run_for drives only this request; callers execute it in the bounded blocking executor or worker.
TwitchService::HttpResult
TwitchService::https_request(const std::string &host, const std::string &target, const std::string &body,
                             bool is_post, const std::vector<std::pair<std::string, std::string>> &headers) {
    if (transport_)
        return transport_(host, target, body, is_post, headers);
    std::string bucket =
        host == "api.twitch.tv" ? "twitch:rate:" + client_id_ + ":app" : "twitch:oauth:rate:" + client_id_;
    if (host == "api.twitch.tv") {
        std::string bearer;
        for (const auto &[name, value] : headers)
            if (name == "Authorization" && value.starts_with("Bearer "))
                bearer = value.substr(7);
        {
            std::lock_guard lock(token_mutex_);
            if (bearer != access_token_)
                bucket = "twitch:rate:" + client_id_ + ":user:" + digest(bearer);
        }
    }
    wait_for_rate_slot(bucket);
    asio::io_context io;
    ssl::context context(ssl::context::tls_client);
    context.set_default_verify_paths();
    context.set_verify_mode(ssl::verify_peer);
    tcp::resolver resolver(io);
    beast::ssl_stream<beast::tcp_stream> stream(io, context);
    stream.set_verify_callback(ssl::host_name_verification(host));
    if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
        throw TwitchError(502, "TLS SNI setup failed");
    http::request<http::string_body> request{is_post ? http::verb::post : http::verb::get, target, 11};
    request.set(http::field::host, host);
    request.set(http::field::user_agent, "ClipStream/1.0");
    for (const auto &[k, v] : headers)
        request.set(k, v);
    if (!body.empty()) {
        request.body() = body;
        if (request.find(http::field::content_type) == request.end())
            request.set(http::field::content_type, "application/x-www-form-urlencoded");
    }
    request.prepare_payload();
    beast::flat_buffer buffer;
    http::response_parser<http::string_body> parser;
    parser.body_limit(8 * 1024 * 1024);
    bool done = false;
    boost::system::error_code failure;
    auto finish = [&](boost::system::error_code ec) {
        failure = ec;
        done = true;
    };
    resolver.async_resolve(host, "443", [&](auto ec, auto endpoints) {
        if (ec)
            return finish(ec);
        beast::get_lowest_layer(stream).async_connect(endpoints, [&](auto ec, auto) {
            if (ec)
                return finish(ec);
            stream.async_handshake(ssl::stream_base::client, [&](auto ec) {
                if (ec)
                    return finish(ec);
                http::async_write(stream, request, [&](auto ec, std::size_t) {
                    if (ec)
                        return finish(ec);
                    http::async_read(stream, buffer, parser, [&](auto ec, std::size_t) { finish(ec); });
                });
            });
        });
    });
    io.run_for(std::chrono::seconds(target.starts_with("/helix/eventsub/subscriptions") ? 8 : 15));
    if (!done) {
        resolver.cancel();
        boost::system::error_code ignored;
        beast::get_lowest_layer(stream).socket().cancel(ignored);
        beast::get_lowest_layer(stream).socket().close(ignored);
        io.restart();
        io.poll();
        throw TwitchError(504, "Twitch request timed out");
    }
    if (failure)
        throw TwitchError(502, "Twitch transport or certificate verification failed");
    auto &response = parser.get();
    HttpResult result;
    result.rate_bucket = bucket;
    result.status = response.result_int();
    result.body = response.body();
    auto header = [&](const char *name) {
        auto it = response.find(name);
        return it == response.end() ? std::string{} : it->value().to_string();
    };
    result.retry_after = header("Retry-After");
    result.ratelimit_remaining = header("Ratelimit-Remaining");
    result.ratelimit_reset = header("Ratelimit-Reset");
    result.ratelimit_limit = header("Ratelimit-Limit");
    return result;
}
void TwitchService::wait_for_rate_slot(const std::string &bucket) {
    {
        std::lock_guard lock(request_mutex_);
        const auto now = std::chrono::steady_clock::now();
        const auto found = rate_resets_.find(bucket);
        if (found != rate_resets_.end() && now < found->second)
            throw TwitchError(
                429, "Twitch rate limited",
                std::max(1,
                         static_cast<int>(
                             std::chrono::duration_cast<std::chrono::seconds>(found->second - now).count())));
        if (!cache_) {
            if (now - last_request_at_ >= std::chrono::minutes(1)) {
                rate_count_ = 0;
                last_request_at_ = now;
            }
            if (++rate_count_ > 600)
                throw TwitchError(429, "Twitch request budget exhausted", 60);
        }
    }
    if (cache_) {
        // Header limits belong to one app/user bucket; a conservative client-wide budget also
        // bounds aggregate traffic across replicas, refreshes and multiple OAuth sessions.
        if (!cache_->acquire_rate_slot(bucket, 600, 60) ||
            (!bucket.starts_with("twitch:oauth:") &&
             !cache_->acquire_rate_slot("twitch:budget:" + client_id_, 600, 60)))
            throw TwitchError(429, "Twitch request budget exhausted", 60);
    }
}
void TwitchService::apply_helix_ratelimit_headers(const HttpResult &res) {
    const auto bucket = res.rate_bucket.empty() ? "twitch:rate:" + client_id_ + ":app" : res.rate_bucket;
    if (cache_ && !res.ratelimit_remaining.empty() && !res.ratelimit_reset.empty() &&
        !res.ratelimit_limit.empty()) {
        try {
            cache_->update_rate_limit(bucket, std::max(0, std::stoi(res.ratelimit_remaining)),
                                      std::stoi(res.ratelimit_reset), std::stoi(res.ratelimit_limit));
        } catch (const std::invalid_argument &) {
        } catch (const std::out_of_range &) {
        }
    }
    if (res.status != 429 && res.ratelimit_remaining != "0")
        return;
    int seconds = 60;
    try {
        if (!res.ratelimit_reset.empty())
            seconds =
                std::clamp(static_cast<int>(std::stoll(res.ratelimit_reset) - std::time(nullptr)), 1, 3600);
        else if (!res.retry_after.empty())
            seconds = std::clamp(std::stoi(res.retry_after), 1, 3600);
    } catch (...) {
    }
    if (cache_ && res.status == 429)
        cache_->update_rate_limit(bucket, 0, static_cast<int>(std::time(nullptr)) + seconds, 600);
    std::lock_guard lock(request_mutex_);
    rate_resets_[bucket] = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    if (rate_resets_.size() > 4096) {
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(rate_resets_, [&](const auto &item) { return item.second <= now; });
    }
}
void TwitchService::require_success(const HttpResult &result) {
    apply_helix_ratelimit_headers(result);
    if (result.status >= 200 && result.status < 300)
        return;
    int retry = result.status == 429 ? 60 : 0;
    try {
        if (!result.retry_after.empty())
            retry = std::clamp(std::stoi(result.retry_after), 1, 3600);
    } catch (...) {
    }
    // Never include upstream response bodies or OAuth values in public errors/logs.
    throw TwitchError(
        result.status == 429 ? 429 : (result.status == 401 ? 401 : (result.status == 403 ? 403 : 502)),
        result.status == 401   ? "Twitch authorization expired or revoked"
        : result.status == 429 ? "Twitch rate limited"
                               : "Twitch upstream request failed",
        retry);
}
bool TwitchService::authenticate_locked() {
    auto response = http_post("/oauth2/token", "client_id=" + encode(client_id_) + "&client_secret=" +
                                                   encode(client_secret_) + "&grant_type=client_credentials");
    auto ts = token_from(response_json(response, false));
    access_token_ = ts.access_token;
    token_expires_at_ = std::chrono::steady_clock::now() + std::chrono::seconds(ts.expires_in);
    return true;
}
bool TwitchService::authenticate() {
    std::lock_guard lock(token_mutex_);
    return authenticate_locked();
}
bool TwitchService::ensure_app_token() {
    std::lock_guard lock(token_mutex_);
    if (!access_token_.empty() &&
        std::chrono::steady_clock::now() + std::chrono::seconds(60) < token_expires_at_)
        return true;
    return authenticate_locked();
}
std::string TwitchService::http_post(const std::string &url, const std::string &body) {
    auto result = https_request("id.twitch.tv", url, body, true, {});
    require_success(result);
    return result.body;
}
std::string TwitchService::http_get(const std::string &url, bool bypass_cache) {
    const auto key = "twitch:helix:v3:" + digest(client_id_ + url);
    if (cache_ && !bypass_cache)
        if (auto cached = cache_->get(key))
            return *cached;
    Flight flight(cache_, "twitch:flight:" + digest(client_id_ + url));
    if (cache_ && !bypass_cache)
        if (auto cached = cache_->get(key))
            return *cached;
    ensure_app_token();
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string token;
        {
            std::lock_guard lock(token_mutex_);
            token = access_token_;
        }
        auto result = https_request("api.twitch.tv", url, {}, false,
                                    {{"Client-Id", client_id_}, {"Authorization", "Bearer " + token}});
        if (result.status == 401 && attempt == 0) {
            std::lock_guard lock(token_mutex_);
            if (access_token_ == token)
                authenticate_locked();
            continue;
        }
        require_success(result);
        response_json(result.body);
        if (cache_ && !bypass_cache)
            cache_->set(key, result.body, 5);
        return result.body;
    }
    throw TwitchError(401, "Twitch app authorization failed");
}
void TwitchService::set_user_token_refresh_handler(std::function<std::string(const std::string &)> handler) {
    user_token_refresh_handler_ = std::move(handler); // Configure before starting request workers.
}
std::string TwitchService::http_get_with_token(const std::string &url, const std::string &original) {
    std::string token = original;
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto result = https_request("api.twitch.tv", url, {}, false,
                                    {{"Client-Id", client_id_}, {"Authorization", "Bearer " + token}});
        if (result.status == 401 && attempt == 0 && user_token_refresh_handler_) {
            token = user_token_refresh_handler_(token);
            if (!token.empty())
                continue;
        }
        require_success(result);
        return result.body;
    }
    throw TwitchError(401, "Twitch user authorization failed");
}
std::string TwitchService::http_post_api(const std::string &url, const std::string &body) {
    auto result = https_request("api.twitch.tv", url, body, true,
                                {{"Client-Id", client_id_},
                                 {"Authorization", "Bearer " + app_access_token()},
                                 {"Content-Type", "application/json"}});
    require_success(result);
    return result.body;
}
json TwitchService::helix_post_json(const std::string &url, const json &body) {
    return response_json(http_post_api(url, body.dump()));
}
json TwitchService::helix_post_user_json(const std::string &url, const json &body,
                                         const std::string &original) {
    std::string token = original;
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto result = https_request("api.twitch.tv", url, body.dump(), true,
                                    {{"Client-Id", client_id_},
                                     {"Authorization", "Bearer " + token},
                                     {"Content-Type", "application/json"}});
        if (result.status == 401 && attempt == 0 && user_token_refresh_handler_) {
            token = user_token_refresh_handler_(token);
            if (!token.empty())
                continue;
        }
        require_success(result);
        return response_json(result.body);
    }
    throw TwitchError(401, "Twitch user authorization failed");
}
std::string TwitchService::app_access_token() {
    ensure_app_token();
    std::lock_guard lock(token_mutex_);
    return access_token_;
}
std::optional<TokenValidation> TwitchService::validate_user_token(const std::string &token) {
    auto result =
        https_request("id.twitch.tv", "/oauth2/validate", {}, false, {{"Authorization", "OAuth " + token}});
    if (result.status == 401)
        return std::nullopt;
    require_success(result);
    auto j = response_json(result.body, false);
    TokenValidation v{str(j, "client_id"), str(j, "user_id"), j.value("expires_in", 0), {}};
    if (j.contains("scopes") && j["scopes"].is_array())
        v.scopes = j["scopes"].get<std::vector<std::string>>();
    if (v.client_id != client_id_ || v.user_id.empty())
        return std::nullopt;
    return v;
}
std::string TwitchService::get_auth_url(const std::string &redirect_uri) const {
    return "https://id.twitch.tv/oauth2/authorize?client_id=" + encode(client_id_) +
           "&redirect_uri=" + encode(redirect_uri) +
           "&response_type=code&scope=user%3Aread%3Aemail%20user%3Aread%3Afollows";
}
std::optional<OAuthTokenSet> TwitchService::exchange_code_tokens(const std::string &code,
                                                                 const std::string &uri) {
    return token_from(response_json(
        http_post("/oauth2/token", "client_id=" + encode(client_id_) +
                                       "&client_secret=" + encode(client_secret_) + "&code=" + encode(code) +
                                       "&grant_type=authorization_code&redirect_uri=" + encode(uri)),
        false));
}
std::string TwitchService::exchange_code(const std::string &code, const std::string &uri) {
    return exchange_code_tokens(code, uri)->access_token;
}
std::optional<OAuthTokenSet> TwitchService::refresh_user_token(const std::string &refresh) {
    if (refresh.empty())
        return std::nullopt;
    auto result = https_request("id.twitch.tv", "/oauth2/token",
                                "grant_type=refresh_token&refresh_token=" + encode(refresh) + "&client_id=" +
                                    encode(client_id_) + "&client_secret=" + encode(client_secret_),
                                true, {});
    if (result.status == 400 || result.status == 401)
        return std::nullopt;
    require_success(result);
    auto ts = token_from(response_json(result.body, false));
    if (ts.refresh_token.empty())
        ts.refresh_token = refresh;
    return ts;
}
std::optional<TwitchUser> TwitchService::get_twitch_user(const std::string &token) {
    auto j = response_json(http_get_with_token("/helix/users", token));
    if (j["data"].empty())
        return std::nullopt;
    return user_from(j["data"][0]);
}
std::unordered_map<std::string, TwitchUser>
TwitchService::fetch_users_by_ids(const std::vector<std::string> &ids) {
    std::unordered_map<std::string, TwitchUser> result;
    std::vector<std::string> missing;
    std::set<std::string> seen;
    for (const auto &id : ids) {
        if (id.empty() || !seen.insert(id).second)
            continue;
        if (cache_) {
            if (auto c = cache_->get("twitch:user:id:" + id)) {
                auto j = json::parse(*c, nullptr, false);
                if (j.is_object()) {
                    try {
                        result[id] = j.get<TwitchUser>();
                        continue;
                    } catch (...) {
                    }
                }
            }
        }
        missing.push_back(id);
    }
    for (std::size_t i = 0; i < missing.size(); i += 100) {
        std::string url = "/helix/users?";
        for (std::size_t k = i; k < std::min(i + 100, missing.size()); ++k)
            url += "&id=" + encode(missing[k]);
        auto j = response_json(http_get(url));
        for (const auto &u : j["data"]) {
            auto user = user_from(u);
            result[user.id] = user;
            if (cache_) {
                cache_->set("twitch:user:id:" + user.id, json(user).dump(), 3600);
                cache_->set("twitch:user:login:" + user.login, json(user).dump(), 3600);
            }
        }
    }
    return result;
}
std::optional<TwitchUser> TwitchService::fetch_user_by_id(const std::string &id) {
    auto users = fetch_users_by_ids({id});
    auto it = users.find(id);
    return it == users.end() ? std::nullopt : std::optional<TwitchUser>(it->second);
}
std::optional<TwitchUser> TwitchService::fetch_user_by_login(const std::string &login) {
    if (login.empty())
        return std::nullopt;
    if (cache_)
        if (auto c = cache_->get("twitch:user:login:" + login)) {
            auto j = json::parse(*c, nullptr, false);
            if (j.is_object())
                try {
                    return j.get<TwitchUser>();
                } catch (...) {
                }
        }
    auto j = response_json(http_get("/helix/users?login=" + encode(login)));
    if (j["data"].empty())
        return std::nullopt;
    auto u = user_from(j["data"][0]);
    if (cache_) {
        cache_->set("twitch:user:id:" + u.id, json(u).dump(), 3600);
        cache_->set("twitch:user:login:" + u.login, json(u).dump(), 3600);
    }
    return u;
}
std::vector<json> TwitchService::get_channels_info(const std::string &token,
                                                   const std::vector<std::string> &ids) {
    std::vector<json> out;
    for (std::size_t i = 0; i < ids.size(); i += 100) {
        std::string url = "/helix/channels?";
        for (std::size_t k = i; k < std::min(i + 100, ids.size()); ++k)
            url += "&broadcaster_id=" + encode(ids[k]);
        auto j = response_json(token.empty() ? http_get(url) : http_get_with_token(url, token));
        for (auto item : j["data"])
            out.push_back(std::move(item));
    }
    return out;
}
std::vector<json> TwitchService::get_followed_channels(const std::string &token, const std::string &id, int) {
    const auto key = "twitch:followed:channels:" + digest(token + ":" + id);
    if (cache_)
        if (auto cached = cache_->get(key)) {
            auto rows = json::parse(*cached, nullptr, false);
            if (rows.is_array())
                return rows.get<std::vector<json>>();
        }
    Flight flight(cache_, key + ":flight");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::vector<json> out;
    std::string cursor;
    std::set<std::string> seen;
    do {
        if (std::chrono::steady_clock::now() >= deadline)
            throw TwitchError(504, "Twitch followed-list pagination exceeded its time budget");
        auto j = response_json(http_get_with_token("/helix/channels/followed?user_id=" + encode(id) +
                                                       "&first=100" +
                                                       (cursor.empty() ? "" : "&after=" + encode(cursor)),
                                                   token));
        for (auto item : j["data"])
            out.push_back(std::move(item));
        cursor = j.value("pagination", json::object()).value("cursor", "");
        if (!cursor.empty() && !seen.insert(cursor).second)
            throw TwitchError(502, "Twitch repeated a pagination cursor");
        if (out.size() > 100000)
            throw TwitchError(502, "Twitch followed list exceeds safety limit");
    } while (!cursor.empty());
    if (cache_)
        cache_->set(key, json(out).dump(), 60);
    return out;
}
std::vector<json> TwitchService::get_followed_streams(const std::string &token, const std::string &id, int) {
    const auto key = "twitch:followed:streams:" + digest(token + ":" + id);
    if (cache_)
        if (auto cached = cache_->get(key)) {
            auto rows = json::parse(*cached, nullptr, false);
            if (rows.is_array())
                return rows.get<std::vector<json>>();
        }
    Flight flight(cache_, key + ":flight");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::vector<json> out;
    std::string cursor;
    std::set<std::string> seen;
    do {
        if (std::chrono::steady_clock::now() >= deadline)
            throw TwitchError(504, "Twitch followed-list pagination exceeded its time budget");
        auto j =
            response_json(http_get_with_token("/helix/streams/followed?user_id=" + encode(id) + "&first=100" +
                                                  (cursor.empty() ? "" : "&after=" + encode(cursor)),
                                              token));
        for (auto item : j["data"])
            out.push_back(std::move(item));
        cursor = j.value("pagination", json::object()).value("cursor", "");
        if (!cursor.empty() && !seen.insert(cursor).second)
            throw TwitchError(502, "Twitch repeated a pagination cursor");
        if (out.size() > 100000)
            throw TwitchError(502, "Twitch followed list exceeds safety limit");
    } while (!cursor.empty());
    if (cache_)
        cache_->set(key, json(out).dump(), 60);
    return out;
}
FetchResult TwitchService::fetch_clips(const std::string &game, int count, const std::string &cursor,
                                       const std::string &start, const std::string &end, int min_views,
                                       bool bypass) {
    count = std::clamp(count, 1, 100);
    min_views = std::max(0, min_views);
    // Cache upstream rows, not a partially deserialized Clip. Every filter shares the same raw contract.
    const std::string key = "twitch:clips:v2:" + encode(game) + ":" + std::to_string(count) + ":" +
                            encode(cursor) + ":" + encode(start) + ":" + encode(end);
    json j;
    if (cache_ && !bypass)
        if (auto c = cache_->get(key)) {
            auto cached = json::parse(*c, nullptr, false);
            if (cached.is_object() && cached.contains("data") && cached["data"].is_array())
                j = std::move(cached);
        }
    if (j.is_null()) {
        std::string url = "/helix/clips?game_id=" + encode(game) + "&first=" + std::to_string(count);
        if (!cursor.empty())
            url += "&after=" + encode(cursor);
        if (!start.empty())
            url += "&started_at=" + encode(start);
        if (!end.empty())
            url += "&ended_at=" + encode(end);
        j = response_json(http_get(url, bypass));
        if (cache_ && !bypass)
            cache_->set(key, j.dump(), 300);
    }
    FetchResult out;
    out.cursor = j.value("pagination", json::object()).value("cursor", "");
    std::vector<std::string> ids;
    for (const auto &row : j["data"])
        if (row.value("view_count", 0) >= min_views) {
            ids.push_back(str(row, "broadcaster_id"));
            ids.push_back(str(row, "creator_id"));
        }
    auto users = fetch_users_by_ids(ids);
    std::vector<std::string> broadcasters;
    for (const auto &row : j["data"])
        if (row.value("view_count", 0) >= min_views)
            broadcasters.push_back(str(row, "broadcaster_id"));
    std::sort(broadcasters.begin(), broadcasters.end());
    broadcasters.erase(std::unique(broadcasters.begin(), broadcasters.end()), broadcasters.end());
    std::unordered_map<std::string, bool> mature;
    for (const auto &row : get_channels_info("", broadcasters))
        mature[str(row, "broadcaster_id")] = !row.contains("content_classification_labels") ||
                                             !row["content_classification_labels"].is_array() ||
                                             !row["content_classification_labels"].empty();
    for (const auto &row : j["data"]) {
        if (row.value("view_count", 0) < min_views)
            continue;
        models::Clip c;
        c.twitch_clip_id = str(row, "id");
        if (c.twitch_clip_id.empty())
            continue;
        c.title = str(row, "title");
        c.thumbnail_url = str(row, "thumbnail_url");
        c.embed_url = str(row, "embed_url");
        c.duration = row.value("duration", 0.0f);
        c.view_count = row.value("view_count", 0u);
        c.broadcaster_name = str(row, "broadcaster_name");
        c.broadcaster_id = str(row, "broadcaster_id");
        c.twitch_created_at = str(row, "created_at");
        c.vod_id = str(row, "video_id");
        c.vod_offset =
            row.contains("vod_offset") && !row["vod_offset"].is_null() ? row["vod_offset"].get<int>() : 0;
        c.broadcaster_lang = str(row, "language");
        c.creator_twitch_id = str(row, "creator_id");
        auto b = users.find(c.broadcaster_id);
        if (b != users.end()) {
            c.broadcaster_login = b->second.login;
            c.broadcaster_avatar = b->second.profile_image_url;
        }
        auto u = users.find(c.creator_twitch_id);
        if (u != users.end())
            c.creator_login = u->second.login;
        // Channel labels describe current channel state, not historical clips. Unknown is restricted
        // conservatively.
        c.classification_source = mature.contains(c.broadcaster_id) ? "current_channel" : "unknown";
        c.is_mature = !mature.contains(c.broadcaster_id) || mature[c.broadcaster_id];
        out.clips.push_back(std::move(c));
    }
    return out;
}
std::vector<GameInfo> TwitchService::fetch_top_games(int count) {
    count = std::clamp(count, 1, 100);
    const std::string key = "twitch:games:top:" + std::to_string(count);
    if (cache_)
        if (auto c = cache_->get(key))
            try {
                return json::parse(*c).get<std::vector<GameInfo>>();
            } catch (...) {
            }
    auto j = response_json(http_get("/helix/games/top?first=" + std::to_string(count)));
    std::vector<GameInfo> out;
    for (const auto &row : j["data"]) {
        GameInfo g{str(row, "id"), str(row, "name"), str(row, "box_art_url")};
        auto p = g.box_art_url.find("{width}");
        if (p != std::string::npos)
            g.box_art_url.replace(p, 7, "285");
        p = g.box_art_url.find("{height}");
        if (p != std::string::npos)
            g.box_art_url.replace(p, 8, "380");
        out.push_back(std::move(g));
    }
    if (cache_)
        cache_->set(key, json(out).dump(), 3600);
    return out;
}
std::vector<json> TwitchService::fetch_live_streams(const std::string &game, int count,
                                                    const std::string &sort, const std::string &language) {
    count = std::clamp(count, 1, 50);
    const auto key = "twitch:streams:" + encode(game) + ":" + std::to_string(count) + ":" + encode(sort) +
                     ":" + encode(language);
    if (cache_)
        if (auto c = cache_->get(key))
            try {
                return json::parse(*c).get<std::vector<json>>();
            } catch (...) {
            }
    std::string url = "/helix/streams?first=" + std::to_string(count);
    if (!game.empty())
        url += "&game_id=" + encode(game);
    if (!language.empty())
        url += "&language=" + encode(language);
    auto j = response_json(http_get(url));
    std::vector<std::string> ids;
    for (const auto &row : j["data"])
        ids.push_back(str(row, "user_id"));
    auto users = fetch_users_by_ids(ids);
    std::vector<json> out;
    for (auto row : j["data"]) {
        auto thumbnail = str(row, "thumbnail_url");
        auto p = thumbnail.find("{width}");
        if (p != std::string::npos)
            thumbnail.replace(p, 7, "640");
        p = thumbnail.find("{height}");
        if (p != std::string::npos)
            thumbnail.replace(p, 8, "360");
        row["thumbnail_url"] = thumbnail;
        auto u = users.find(str(row, "user_id"));
        row["avatar_url"] = u == users.end() ? "" : u->second.profile_image_url;
        out.push_back(std::move(row));
    }
    if (sort == "random") {
        std::mt19937 rng(std::random_device{}());
        std::shuffle(out.begin(), out.end(), rng);
    }
    if (sort == "started_at")
        std::sort(out.begin(), out.end(),
                  [](const auto &a, const auto &b) { return str(a, "started_at") > str(b, "started_at"); });
    if (cache_)
        cache_->set(key, json(out).dump(), 60);
    return out;
}
std::vector<json> TwitchService::search_channels(const std::string &query, int count) {
    if (query.empty())
        return {};
    const auto key =
        "twitch:search:channels:" + encode(query) + ":" + std::to_string(std::clamp(count, 1, 100));
    if (cache_)
        if (auto c = cache_->get(key))
            try {
                return json::parse(*c).get<std::vector<json>>();
            } catch (...) {
            }
    auto j = response_json(http_get("/helix/search/channels?query=" + encode(query) +
                                    "&first=" + std::to_string(std::clamp(count, 1, 100))));
    auto out = j["data"].get<std::vector<json>>();
    if (cache_)
        cache_->set(key, json(out).dump(), 60);
    return out;
}
std::unordered_set<std::string> TwitchService::check_live_by_logins(const std::vector<std::string> &names) {
    std::unordered_set<std::string> out;
    for (std::size_t i = 0; i < names.size(); i += 100) {
        std::string url = "/helix/streams?first=100";
        for (std::size_t k = i; k < std::min(i + 100, names.size()); ++k)
            url += "&user_login=" + encode(names[k]);
        auto j = response_json(http_get(url));
        for (const auto &row : j["data"]) {
            out.insert(str(row, "user_login"));
            out.insert(str(row, "user_name"));
        }
    }
    return out;
}
std::unordered_set<std::string> TwitchService::check_live_by_user_ids(const std::vector<std::string> &ids) {
    std::unordered_set<std::string> out;
    for (std::size_t i = 0; i < ids.size(); i += 100) {
        std::string url = "/helix/streams?first=100";
        for (std::size_t k = i; k < std::min(i + 100, ids.size()); ++k)
            url += "&user_id=" + encode(ids[k]);
        auto j = response_json(http_get(url));
        for (const auto &row : j["data"])
            out.insert(str(row, "user_id"));
    }
    return out;
}
json TwitchService::get_public_channel_profile(const std::string &login) {
    if (login.empty())
        return {{"found", false}};
    const auto key = "twitch:pub_profile:" + login;
    if (cache_)
        if (auto c = cache_->get(key)) {
            auto j = json::parse(*c, nullptr, false);
            if (j.is_object())
                return j;
        }
    auto j = response_json(http_get("/helix/users?login=" + encode(login)));
    if (j["data"].empty())
        return {{"found", false}};
    auto u = j["data"][0];
    const auto id = str(u, "id");
    auto streams = response_json(http_get("/helix/streams?user_id=" + encode(id)));
    u["is_live"] = !streams["data"].empty();
    if (!streams["data"].empty()) {
        u["stream"] = streams["data"][0];
        auto thumbnail = str(u["stream"], "thumbnail_url");
        auto position = thumbnail.find("{width}");
        if (position != std::string::npos)
            thumbnail.replace(position, 7, "1280");
        position = thumbnail.find("{height}");
        if (position != std::string::npos)
            thumbnail.replace(position, 8, "720");
        u["stream"]["thumbnail_url"] = thumbnail;
    }
    auto channels = get_channels_info("", {id});
    if (!channels.empty())
        u["channel"] = channels[0];
    json out = {{"found", true}, {"twitch", u}};
    if (cache_)
        cache_->set(key, out.dump(), 60);
    return out;
}
} // namespace clipstream::services
