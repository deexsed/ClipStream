#include "services/TwitchService.hpp"
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/ssl.h>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <cctype>
#include <random>
#include <ctime>

namespace clipstream::services {

namespace beast = boost::beast;
namespace http  = beast::http;
namespace asio  = boost::asio;
namespace ssl   = asio::ssl;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

namespace {

std::string encode_query_component(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::string o;
    o.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '_' || c == '-') o += static_cast<char>(c);
        else {
            static const char* hex = "0123456789ABCDEF";
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 0xF];
        }
    }
    return o;
}

} // namespace

TwitchService::TwitchService(const std::string& client_id, const std::string& client_secret, CacheService* cache)
    : client_id_(client_id), client_secret_(client_secret), cache_(cache) {}

TwitchService::HttpResult TwitchService::https_request(const std::string& host, const std::string& target,
                                                       const std::string& body, bool is_post,
                                                       const std::vector<std::pair<std::string, std::string>>& headers) {
    HttpResult out;
    try {
        wait_for_rate_slot();

        asio::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_none);

        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
        SSL_set_tlsext_host_name(stream.native_handle(), host.c_str());

        auto results = resolver.resolve(host, "443");
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);

        http::request<http::string_body> req{is_post ? http::verb::post : http::verb::get, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "ClipStream/1.0");

        for (auto& [k, v] : headers)
            req.set(k, v);

        if (!body.empty()) {
            req.body() = body;
            if (req.find(http::field::content_type) == req.end())
                req.set(http::field::content_type, "application/x-www-form-urlencoded");
        }
        req.prepare_payload();

        http::write(stream, req);

        beast::flat_buffer buf;
        http::response<http::string_body> res;
        http::read(stream, buf, res);
        out.status = static_cast<int>(res.result_int());
        out.body = res.body();
        auto rh = res.find("Retry-After");
        if (rh != res.end()) out.retry_after = rh->value().to_string();
        auto lim = res.find("Ratelimit-Limit");
        if (lim != res.end()) out.ratelimit_limit = lim->value().to_string();
        auto rem = res.find("Ratelimit-Remaining");
        if (rem != res.end()) out.ratelimit_remaining = rem->value().to_string();
        auto reset = res.find("Ratelimit-Reset");
        if (reset != res.end()) out.ratelimit_reset = reset->value().to_string();
        return out;
    } catch (const std::exception& e) {
        std::cerr << "[Twitch] HTTPS " << host << target << " error: " << e.what() << "\n";
        out.body = "{}";
        return out;
    }
}

void TwitchService::wait_for_rate_slot() {
    std::lock_guard<std::mutex> lock(request_mutex_);
    const auto now = std::chrono::steady_clock::now();
    // Token bucket Helix: см. https://dev.twitch.tv/docs/api/guide#twitch-rate-limits
    // (Ratelimit-Limit / Remaining / Reset; 429 + Retry-After). Для app access token часто ~800 очков/мин,
    // дефолтный вес запроса 1 → не чаще ~13.3 запросов/с между вызовами (~75 мс).
    const auto min_gap = std::chrono::milliseconds(75);
    if (last_request_at_.time_since_epoch().count() != 0) {
        const auto elapsed = now - last_request_at_;
        if (elapsed < min_gap) std::this_thread::sleep_for(min_gap - elapsed);
    }
    last_request_at_ = std::chrono::steady_clock::now();
}

void TwitchService::apply_helix_ratelimit_headers(const HttpResult& res) {
    if (res.ratelimit_remaining.empty() || res.ratelimit_reset.empty()) return;
    try {
        const int remaining = std::stoi(res.ratelimit_remaining);
        const long long reset_unix = std::stoll(res.ratelimit_reset);
        if (remaining <= 0) {
            const auto now = static_cast<long long>(std::time(nullptr));
            const auto wait_sec = std::max(1LL, reset_unix - now);
            std::this_thread::sleep_for(std::chrono::seconds(wait_sec));
        }
    } catch (...) {}
}

bool TwitchService::ensure_app_token() {
    std::lock_guard<std::mutex> lock(token_mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (!access_token_.empty() && now + std::chrono::minutes(1) < token_expires_at_) return true;
    return authenticate();
}

std::string TwitchService::http_post(const std::string& url, const std::string& body) {
    constexpr int kMaxAttempts = 3;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        auto res = https_request("id.twitch.tv", url, body, true, {});
        if (res.status == 429) {
            int retry_sec = 1;
            if (!res.retry_after.empty()) {
                try { retry_sec = std::max(1, std::stoi(res.retry_after)); } catch (...) {}
            }
            std::this_thread::sleep_for(std::chrono::seconds(retry_sec));
            continue;
        }
        return res.body.empty() ? "{}" : res.body;
    }
    return "{}";
}

std::string TwitchService::http_get(const std::string& url) {
    if (!ensure_app_token()) return "{}";

    constexpr int kMaxAttempts = 3;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        std::string token;
        {
            std::lock_guard<std::mutex> lock(token_mutex_);
            token = access_token_;
        }
        auto res = https_request("api.twitch.tv", url, "", false, {
            {"Client-Id", client_id_},
            {"Authorization", "Bearer " + token},
        });

        if (res.status == 401) {
            // App token expired/revoked; refresh and retry.
            {
                std::lock_guard<std::mutex> lock(token_mutex_);
                access_token_.clear();
                token_expires_at_ = std::chrono::steady_clock::time_point{};
            }
            if (!ensure_app_token()) break;
            continue;
        }

        if (res.status == 429) {
            int retry_sec = 1;
            if (!res.retry_after.empty()) {
                try { retry_sec = std::max(1, std::stoi(res.retry_after)); } catch (...) {}
            }
            std::this_thread::sleep_for(std::chrono::seconds(retry_sec));
            continue;
        }

        apply_helix_ratelimit_headers(res);

        return res.body.empty() ? "{}" : res.body;
    }
    return "{}";
}

std::string TwitchService::http_get_with_token(const std::string& url, const std::string& token) {
    constexpr int kMaxAttempts = 3;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        auto res = https_request("api.twitch.tv", url, "", false, {
            {"Client-Id", client_id_},
            {"Authorization", "Bearer " + token},
        });

        if (res.status == 429) {
            int retry_sec = 1;
            if (!res.retry_after.empty()) {
                try { retry_sec = std::max(1, std::stoi(res.retry_after)); } catch (...) {}
            }
            std::this_thread::sleep_for(std::chrono::seconds(retry_sec));
            continue;
        }

        apply_helix_ratelimit_headers(res);
        return res.body.empty() ? "{}" : res.body;
    }
    return "{}";
}

std::string TwitchService::http_post_api(const std::string& url, const std::string& body) {
    if (!ensure_app_token()) return "{}";

    constexpr int kMaxAttempts = 3;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        std::string token;
        {
            std::lock_guard<std::mutex> lock(token_mutex_);
            token = access_token_;
        }
        auto res = https_request("api.twitch.tv", url, body, true, {
            {"Client-Id", client_id_},
            {"Authorization", "Bearer " + token},
            {"Content-Type", "application/json"},
        });

        if (res.status == 401) {
            {
                std::lock_guard<std::mutex> lock(token_mutex_);
                access_token_.clear();
                token_expires_at_ = std::chrono::steady_clock::time_point{};
            }
            if (!ensure_app_token()) break;
            continue;
        }
        if (res.status == 429) {
            int retry_sec = 1;
            if (!res.retry_after.empty()) {
                try { retry_sec = std::max(1, std::stoi(res.retry_after)); } catch (...) {}
            }
            std::this_thread::sleep_for(std::chrono::seconds(retry_sec));
            continue;
        }

        apply_helix_ratelimit_headers(res);
        return res.body.empty() ? "{}" : res.body;
    }
    return "{}";
}

bool TwitchService::authenticate() {
    std::string body = "client_id=" + client_id_
        + "&client_secret=" + client_secret_
        + "&grant_type=client_credentials";

    auto response = http_post("/oauth2/token", body);
    try {
        auto j = json::parse(response);
        if (j.contains("access_token")) {
            access_token_ = j["access_token"];
            int expires = j.value("expires_in", 3600);
            // Refresh a bit early to avoid race near expiry.
            token_expires_at_ = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(60, expires - 120));
            std::cout << "[Twitch] Authenticated successfully\n";
            return true;
        }
        std::cerr << "[Twitch] Auth response: " << response << "\n";
    } catch (...) {}
    std::cerr << "[Twitch] Authentication failed\n";
    return false;
}

FetchResult TwitchService::fetch_clips(const std::string& game_id, int count,
                                       const std::string& cursor,
                                       const std::string& started_at,
                                       const std::string& ended_at,
                                       int min_views,
                                       bool bypass_response_cache) {
    const std::string cache_key = "twitch:clips:" + game_id + ":" + std::to_string(std::min(count, 100))
        + ":" + cursor + ":" + started_at + ":" + ended_at;
    if (cache_ && !bypass_response_cache) {
        auto c = cache_->get(cache_key);
        if (c) {
            try {
                FetchResult cached_result;
                auto cj = json::parse(*c);
                if (cj.contains("cursor")) cached_result.cursor = cj.value("cursor", "");
                if (cj.contains("clips") && cj["clips"].is_array()) {
                    for (auto& item : cj["clips"]) {
                        models::Clip clip;
                        clip.id = item.value("id", 0ULL);
                        clip.twitch_clip_id = item.value("twitch_clip_id", "");
                        clip.title = item.value("title", "");
                        clip.thumbnail_url = item.value("thumbnail_url", "");
                        clip.embed_url = item.value("embed_url", "");
                        clip.duration = item.value("duration", 0.0f);
                        clip.view_count = item.value("view_count", 0u);
                        clip.like_count = item.value("like_count", 0);
                        clip.dislike_count = item.value("dislike_count", 0);
                        clip.broadcaster_name = item.value("broadcaster_name", "");
                        clip.broadcaster_id = item.value("broadcaster_id", "");
                        clip.broadcaster_avatar = item.value("broadcaster_avatar", "");
                        clip.game_id = item.value("game_id", 0ULL);
                        clip.game_name = item.value("game_name", "");
                        clip.created_at = item.value("created_at", "");
                        clip.vod_id = item.value("vod_id", "");
                        clip.vod_offset = item.value("vod_offset", 0);
                        clip.twitch_created_at = item.value("twitch_created_at", "");
                        clip.broadcaster_lang  = item.value("language", std::string{});
                        clip.creator_twitch_id = item.value("creator_twitch_id", "");
                        clip.creator_login     = item.value("creator_login", "");
                        cached_result.clips.push_back(std::move(clip));
                    }
                }
                return cached_result;
            } catch (...) {}
        }
    }

    std::string url = "/helix/clips?game_id=" + game_id
        + "&first=" + std::to_string(std::min(count, 100));
    if (!cursor.empty()) url += "&after=" + cursor;
    if (!started_at.empty()) url += "&started_at=" + started_at;
    if (!ended_at.empty()) url += "&ended_at=" + ended_at;

    auto response = http_get(url);
    FetchResult result;

    auto safe_str = [](const json& j, const char* key) -> std::string {
        if (!j.contains(key) || j[key].is_null()) return "";
        return j[key].get<std::string>();
    };
    auto safe_float = [](const json& j, const char* key) -> float {
        if (!j.contains(key) || j[key].is_null()) return 0.0f;
        return j[key].get<float>();
    };
    auto safe_uint = [](const json& j, const char* key) -> uint32_t {
        if (!j.contains(key) || j[key].is_null()) return 0;
        return j[key].get<uint32_t>();
    };
    auto safe_int = [](const json& j, const char* key) -> int {
        if (!j.contains(key) || j[key].is_null()) return 0;
        return j[key].get<int>();
    };
    auto normalize_lang = [](std::string s) -> std::string {
        std::string o;
        o.reserve(s.size());
        for (char ch : s) {
            if (ch >= 'A' && ch <= 'Z') o += static_cast<char>(ch - 'A' + 'a');
            else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')
                o += ch;
            if (o.size() >= 16) break;
        }
        return o;
    };

    try {
        auto j = json::parse(response);
        if (!j.contains("data") || j["data"].is_null() || !j["data"].is_array()) {
            std::cerr << "[Twitch] No clip data for game " << game_id
                      << ", response: " << response.substr(0, 200) << "\n";
            return result;
        }

        for (auto& item : j["data"]) {
            try {
                models::Clip clip;
                clip.twitch_clip_id    = safe_str(item, "id");
                clip.title             = safe_str(item, "title");
                clip.thumbnail_url     = safe_str(item, "thumbnail_url");
                clip.embed_url         = safe_str(item, "embed_url");
                clip.duration          = safe_float(item, "duration");
                clip.view_count        = safe_uint(item, "view_count");
                clip.broadcaster_name  = safe_str(item, "broadcaster_name");
                clip.broadcaster_id    = safe_str(item, "broadcaster_id");
                clip.twitch_created_at = safe_str(item, "created_at");
                clip.vod_id            = safe_str(item, "video_id");
                clip.vod_offset        = safe_int(item, "vod_offset");
                clip.broadcaster_lang  = normalize_lang(safe_str(item, "language"));
                clip.creator_twitch_id = safe_str(item, "creator_id");
                clip.creator_login     = safe_str(item, "creator_name");
                if (!clip.twitch_clip_id.empty() && static_cast<int>(clip.view_count) >= min_views)
                    result.clips.push_back(std::move(clip));
            } catch (const std::exception& e) {
                std::cerr << "[Twitch] Skip clip: " << e.what() << "\n";
            }
        }

        if (j.contains("pagination") && j["pagination"].contains("cursor")
            && !j["pagination"]["cursor"].is_null())
            result.cursor = j["pagination"]["cursor"].get<std::string>();

        std::cout << "[Twitch] Fetched " << result.clips.size()
                  << " clips for game " << game_id << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[Twitch] Parse error: " << e.what() << "\n";
    }

    if (cache_ && !bypass_response_cache) {
        try {
            json out;
            out["cursor"] = result.cursor;
            out["clips"] = json::array();
            for (const auto& c : result.clips) out["clips"].push_back(c);
            cache_->set(cache_key, out.dump(), 300);
        } catch (...) {}
    }

    return result;
}

std::vector<GameInfo> TwitchService::fetch_top_games(int count) {
    const std::string cache_key = "twitch:games:top:" + std::to_string(std::min(count, 100));
    if (cache_) {
        auto c = cache_->get(cache_key);
        if (c) {
            try {
                auto j = json::parse(*c);
                return j.get<std::vector<GameInfo>>();
            } catch (...) {}
        }
    }

    std::string url = "/helix/games/top?first=" + std::to_string(std::min(count, 100));
    auto response = http_get(url);
    std::vector<GameInfo> games;

    try {
        auto j = json::parse(response);
        if (!j.contains("data") || j["data"].is_null() || !j["data"].is_array()) return games;

        for (auto& item : j["data"]) {
            try {
            GameInfo g;
            g.twitch_id   = (!item.contains("id")   || item["id"].is_null())   ? "" : item["id"].get<std::string>();
            g.name        = (!item.contains("name")  || item["name"].is_null()) ? "" : item["name"].get<std::string>();
            g.box_art_url = (!item.contains("box_art_url") || item["box_art_url"].is_null()) ? "" : item["box_art_url"].get<std::string>();
            // Replace template dimensions
            auto& url = g.box_art_url;
            auto pos1 = url.find("{width}");
            if (pos1 != std::string::npos) url.replace(pos1, 7, "285");
            auto pos2 = url.find("{height}");
            if (pos2 != std::string::npos) url.replace(pos2, 8, "380");
            if (!g.twitch_id.empty())
                games.push_back(std::move(g));
            } catch (const std::exception& e) {
                std::cerr << "[Twitch] Skip game: " << e.what() << "\n";
            }
        }
        std::cout << "[Twitch] Fetched " << games.size() << " top games\n";
    } catch (const std::exception& e) {
        std::cerr << "[Twitch] Top games parse error: " << e.what() << "\n";
    }

    if (cache_) {
        try { cache_->set(cache_key, json(games).dump(), 3600); } catch (...) {}
    }
    return games;
}

std::string TwitchService::get_auth_url(const std::string& redirect_uri) const {
    return "https://id.twitch.tv/oauth2/authorize"
           "?client_id=" + client_id_ +
           "&redirect_uri=" + redirect_uri +
           "&response_type=code"
           "&scope=user:read:email+user:read:follows";
}

std::string TwitchService::exchange_code(const std::string& code, const std::string& redirect_uri) {
    std::string body = "client_id=" + client_id_
        + "&client_secret=" + client_secret_
        + "&code=" + code
        + "&grant_type=authorization_code"
        + "&redirect_uri=" + redirect_uri;

    auto response = http_post("/oauth2/token", body);
    try {
        auto j = json::parse(response);
        if (j.contains("access_token"))
            return j["access_token"].get<std::string>();
        std::cerr << "[Twitch] Exchange code error: " << response << "\n";
    } catch (...) {}
    return "";
}

std::optional<OAuthTokenSet> TwitchService::exchange_code_tokens(const std::string& code, const std::string& redirect_uri) {
    std::string body = "client_id=" + client_id_
        + "&client_secret=" + client_secret_
        + "&code=" + code
        + "&grant_type=authorization_code"
        + "&redirect_uri=" + redirect_uri;

    auto response = http_post("/oauth2/token", body);
    auto j = json::parse(response, nullptr, false);
    if (j.is_discarded() || !j.contains("access_token")) return std::nullopt;

    OAuthTokenSet ts;
    ts.access_token = j.value("access_token", "");
    ts.refresh_token = j.value("refresh_token", "");
    ts.expires_in = j.value("expires_in", 0);
    if (j.contains("scope") && j["scope"].is_array()) {
        bool first = true;
        for (const auto& s : j["scope"]) {
            if (!s.is_string()) continue;
            if (!first) ts.scope += " ";
            ts.scope += s.get<std::string>();
            first = false;
        }
    }
    if (ts.access_token.empty()) return std::nullopt;
    return ts;
}

std::optional<OAuthTokenSet> TwitchService::refresh_user_token(const std::string& refresh_token) {
    if (refresh_token.empty()) return std::nullopt;
    std::string body = "grant_type=refresh_token"
        + std::string("&refresh_token=") + refresh_token
        + "&client_id=" + client_id_
        + "&client_secret=" + client_secret_;

    auto response = http_post("/oauth2/token", body);
    auto j = json::parse(response, nullptr, false);
    if (j.is_discarded() || !j.contains("access_token")) return std::nullopt;

    OAuthTokenSet ts;
    ts.access_token = j.value("access_token", "");
    ts.refresh_token = j.value("refresh_token", refresh_token);
    ts.expires_in = j.value("expires_in", 0);
    if (j.contains("scope") && j["scope"].is_array()) {
        bool first = true;
        for (const auto& s : j["scope"]) {
            if (!s.is_string()) continue;
            if (!first) ts.scope += " ";
            ts.scope += s.get<std::string>();
            first = false;
        }
    }
    if (ts.access_token.empty()) return std::nullopt;
    return ts;
}

std::optional<TwitchUser> TwitchService::get_twitch_user(const std::string& user_token) {
    auto response = http_get_with_token("/helix/users", user_token);
    try {
        auto j = json::parse(response);
        if (!j.contains("data") || j["data"].empty()) return std::nullopt;

        auto& u = j["data"][0];
        TwitchUser user;
        user.id                = u.value("id", "");
        user.login             = u.value("login", "");
        user.display_name      = u.value("display_name", "");
        user.email             = u.value("email", "");
        user.profile_image_url = u.value("profile_image_url", "");
        return user;
    } catch (...) {}
    return std::nullopt;
}

std::vector<nlohmann::json> TwitchService::get_followed_channels(const std::string& user_token, const std::string& user_id, int count) {
    if (user_token.empty() || user_id.empty()) return {};
    std::string url = "/helix/channels/followed?user_id=" + user_id + "&first=" + std::to_string(std::max(1, std::min(count, 100)));
    auto response = http_get_with_token(url, user_token);
    auto j = json::parse(response, nullptr, false);
    if (j.is_discarded() || !j.contains("data") || !j["data"].is_array()) return {};
    std::vector<json> out;
    for (const auto& item : j["data"]) out.push_back(item);
    return out;
}

std::vector<nlohmann::json> TwitchService::get_followed_streams(const std::string& user_token, const std::string& user_id, int count) {
    if (user_token.empty() || user_id.empty()) return {};
    std::string url = "/helix/streams/followed?user_id=" + user_id + "&first=" + std::to_string(std::max(1, std::min(count, 100)));
    auto response = http_get_with_token(url, user_token);
    auto j = json::parse(response, nullptr, false);
    if (j.is_discarded() || !j.contains("data") || !j["data"].is_array()) return {};
    std::vector<json> out;
    for (const auto& item : j["data"]) out.push_back(item);
    return out;
}

std::vector<nlohmann::json> TwitchService::get_channels_info(const std::string& user_token, const std::vector<std::string>& broadcaster_ids) {
    if (user_token.empty() || broadcaster_ids.empty()) return {};
    std::vector<json> out;
    for (size_t i = 0; i < broadcaster_ids.size(); i += 100) {
        std::string url = "/helix/channels?";
        bool first = true;
        for (size_t j = i; j < broadcaster_ids.size() && j < i + 100; ++j) {
            if (!first) url += "&";
            url += "broadcaster_id=" + broadcaster_ids[j];
            first = false;
        }
        auto response = http_get_with_token(url, user_token);
        auto parsed = json::parse(response, nullptr, false);
        if (parsed.is_discarded() || !parsed.contains("data") || !parsed["data"].is_array()) continue;
        for (const auto& item : parsed["data"]) out.push_back(item);
    }
    return out;
}

std::optional<TwitchUser> TwitchService::fetch_user_by_login(const std::string& login) {
    if (login.empty()) return std::nullopt;
    const std::string cache_key = "twitch:user:login:" + login;
    if (cache_) {
        auto c = cache_->get(cache_key);
        if (c) {
            try { return json::parse(*c).get<TwitchUser>(); } catch (...) {}
        }
    }
    std::string url = "/helix/users?login=" + login;
    auto response = http_get(url);
    try {
        auto j = json::parse(response);
        if (!j.contains("data") || j["data"].empty()) return std::nullopt;
        auto& u = j["data"][0];
        TwitchUser user;
        user.id                = u.value("id", "");
        user.login             = u.value("login", "");
        user.display_name      = u.value("display_name", "");
        user.email             = ""; // email не приходит в app-token запросах
        user.profile_image_url = u.value("profile_image_url", "");
        if (cache_) {
            try { cache_->set(cache_key, json(user).dump(), 24 * 3600); } catch (...) {}
        }
        return user;
    } catch (...) {}
    return std::nullopt;
}

std::optional<TwitchUser> TwitchService::fetch_user_by_id(const std::string& id) {
    if (id.empty()) return std::nullopt;
    const std::string cache_key = "twitch:user:id:" + id;
    if (cache_) {
        auto c = cache_->get(cache_key);
        if (c) {
            try { return json::parse(*c).get<TwitchUser>(); } catch (...) {}
        }
    }
    std::string url = "/helix/users?id=" + id;
    auto response = http_get(url);
    try {
        auto j = json::parse(response);
        if (!j.contains("data") || j["data"].empty()) return std::nullopt;
        auto& u = j["data"][0];
        TwitchUser user;
        user.id                = u.value("id", "");
        user.login             = u.value("login", "");
        user.display_name      = u.value("display_name", "");
        user.email             = "";
        user.profile_image_url = u.value("profile_image_url", "");
        if (cache_) {
            try { cache_->set(cache_key, json(user).dump(), 24 * 3600); } catch (...) {}
        }
        return user;
    } catch (...) {}
    return std::nullopt;
}

std::vector<nlohmann::json> TwitchService::fetch_live_streams(const std::string& game_twitch_id, int count,
                                                              const std::string& sort, const std::string& language) {
    const std::string cache_key = "twitch:streams:" + game_twitch_id + ":" + std::to_string(std::max(1, std::min(count, 50)))
        + ":" + sort + ":" + language;
    if (cache_) {
        auto c = cache_->get(cache_key);
        if (c) {
            try { return json::parse(*c).get<std::vector<json>>(); } catch (...) {}
        }
    }

    std::string url = "/helix/streams?first=" + std::to_string(std::max(1, std::min(count, 50)));
    if (!game_twitch_id.empty()) url += "&game_id=" + game_twitch_id;
    if (!language.empty()) url += "&language=" + language;

    auto response = http_get(url);
    std::vector<nlohmann::json> streams;

    try {
        auto j = json::parse(response);
        if (!j.contains("data") || !j["data"].is_array()) return streams;

        std::vector<std::string> user_ids;
        user_ids.reserve(j["data"].size());

        for (auto& item : j["data"]) {
            if (item.contains("user_id") && !item["user_id"].is_null())
                user_ids.push_back(item["user_id"].get<std::string>());
        }

        std::unordered_map<std::string, std::string> avatars;
        if (!user_ids.empty()) {
            std::string users_url = "/helix/users";
            for (const auto& id : user_ids) users_url += "&id=" + id;
            // fix first separator if at least one id exists
            users_url.replace(users_url.find("&id="), 1, "?");

            auto users_response = http_get(users_url);
            auto uj = json::parse(users_response, nullptr, false);
            if (!uj.is_discarded() && uj.contains("data") && uj["data"].is_array()) {
                for (auto& u : uj["data"]) {
                    if (u.contains("id") && u.contains("profile_image_url")
                        && !u["id"].is_null() && !u["profile_image_url"].is_null()) {
                        avatars[u["id"].get<std::string>()] = u["profile_image_url"].get<std::string>();
                    }
                }
            }
        }

        for (auto& item : j["data"]) {
            auto thumb = item.value("thumbnail_url", std::string{});
            auto p1 = thumb.find("{width}");
            if (p1 != std::string::npos) thumb.replace(p1, 7, "640");
            auto p2 = thumb.find("{height}");
            if (p2 != std::string::npos) thumb.replace(p2, 8, "360");

            auto uid = item.value("user_id", std::string{});
            std::string avatar = "";
            auto it = avatars.find(uid);
            if (it != avatars.end()) avatar = it->second;

            streams.push_back({
                {"id", item.value("id", "")},
                {"user_id", uid},
                {"user_login", item.value("user_login", "")},
                {"user_name", item.value("user_name", "")},
                {"title", item.value("title", "")},
                {"viewer_count", item.value("viewer_count", 0)},
                {"thumbnail_url", thumb},
                {"started_at", item.value("started_at", "")},
                {"game_id", item.value("game_id", "")},
                {"game_name", item.value("game_name", "")},
                {"avatar_url", avatar}
            });
        }

        if (sort == "random") {
            std::mt19937 rng(std::random_device{}());
            std::shuffle(streams.begin(), streams.end(), rng);
        } else if (sort == "started_at") {
            std::sort(streams.begin(), streams.end(), [](const json& a, const json& b) {
                return a.value("started_at", std::string{}) > b.value("started_at", std::string{});
            });
        } else {
            std::sort(streams.begin(), streams.end(), [](const json& a, const json& b) {
                return a.value("viewer_count", 0) > b.value("viewer_count", 0);
            });
        }
    } catch (const std::exception& e) {
        std::cerr << "[Twitch] Live streams parse error: " << e.what() << "\n";
    }
    if (cache_) {
        try { cache_->set(cache_key, json(streams).dump(), 90); } catch (...) {}
    }
    return streams;
}

std::vector<nlohmann::json> TwitchService::search_channels(const std::string& query, int count) {
    if (query.empty()) return {};
    const int limit = std::max(1, std::min(count, 100));
    const std::string cache_key = "twitch:search:channels:" + query + ":" + std::to_string(limit);
    if (cache_) {
        auto c = cache_->get(cache_key);
        if (c) {
            auto j = json::parse(*c, nullptr, false);
            if (!j.is_discarded() && j.is_array()) return j.get<std::vector<json>>();
        }
    }

    std::string encoded_query = query;
    for (char& ch : encoded_query) if (ch == ' ') ch = '+';
    std::string url = "/helix/search/channels?query=" + encoded_query + "&first=" + std::to_string(limit);
    auto response = http_get(url);
    std::vector<json> out;
    auto j = json::parse(response, nullptr, false);
    if (!j.is_discarded() && j.contains("data") && j["data"].is_array()) {
        for (auto& item : j["data"]) {
            out.push_back({
                {"id", item.value("id", "")},
                {"broadcaster_login", item.value("broadcaster_login", "")},
                {"display_name", item.value("display_name", "")},
                {"title", item.value("title", "")},
                {"thumbnail_url", item.value("thumbnail_url", "")},
                {"is_live", item.value("is_live", false)},
                {"game_name", item.value("game_name", "")},
                {"started_at", item.value("started_at", "")}
            });
        }
    }
    if (cache_) {
        try { cache_->set(cache_key, json(out).dump(), 300); } catch (...) {}
    }
    return out;
}

std::string TwitchService::app_access_token() {
    if (!ensure_app_token()) return "";
    std::lock_guard<std::mutex> lock(token_mutex_);
    return access_token_;
}

nlohmann::json TwitchService::helix_post_json(const std::string& url, const nlohmann::json& body) {
    auto raw = http_post_api(url, body.dump());
    return json::parse(raw, nullptr, false);
}

std::unordered_set<std::string> TwitchService::check_live_by_logins(const std::vector<std::string>& logins) {
    std::unordered_set<std::string> result;
    if (logins.empty()) return result;

    std::string cache_key = "twitch:live_check:";
    for (auto& l : logins) cache_key += l + ",";
    if (cache_) {
        auto c = cache_->get(cache_key);
        if (c) {
            auto j = json::parse(*c, nullptr, false);
            if (j.is_array()) {
                for (auto& item : j) result.insert(item.get<std::string>());
            }
            return result;
        }
    }

    // Twitch allows up to 100 user_login params per request
    std::string url = "/helix/streams?first=100";
    for (auto& login : logins) {
        url += "&user_login=" + login;
    }

    auto response = http_get(url);
    auto j = json::parse(response, nullptr, false);
    if (!j.is_discarded() && j.contains("data") && j["data"].is_array()) {
        for (auto& item : j["data"]) {
            auto login = item.value("user_login", "");
            auto name = item.value("user_name", "");
            if (!login.empty()) result.insert(login);
            if (!name.empty()) result.insert(name);
        }
    }

    if (cache_) {
        json arr = json::array();
        for (auto& s : result) arr.push_back(s);
        try { cache_->set(cache_key, arr.dump(), 60); } catch (...) {}
    }

    return result;
}

std::unordered_set<std::string> TwitchService::check_live_by_user_ids(const std::vector<std::string>& user_ids) {
    std::unordered_set<std::string> result;
    if (user_ids.empty()) return result;

    std::string cache_key = "twitch:live_check_ids:";
    for (auto& id : user_ids) cache_key += id + ",";
    if (cache_) {
        auto c = cache_->get(cache_key);
        if (c) {
            auto j = json::parse(*c, nullptr, false);
            if (j.is_array()) {
                for (auto& item : j) result.insert(item.get<std::string>());
            }
            return result;
        }
    }

    for (size_t i = 0; i < user_ids.size(); i += 100) {
        std::string url = "/helix/streams?first=100";
        for (size_t j = i; j < user_ids.size() && j < i + 100; ++j) {
            url += "&user_id=" + user_ids[j];
        }
        auto response = http_get(url);
        auto j = json::parse(response, nullptr, false);
        if (j.is_discarded() || !j.contains("data") || !j["data"].is_array()) continue;
        for (auto& item : j["data"]) {
            auto id = item.value("user_id", "");
            if (!id.empty()) result.insert(id);
        }
    }

    if (cache_) {
        json arr = json::array();
        for (auto& s : result) arr.push_back(s);
        try { cache_->set(cache_key, arr.dump(), 60); } catch (...) {}
    }
    return result;
}

nlohmann::json TwitchService::get_public_channel_profile(const std::string& login) {
    json out;
    if (login.empty()) {
        out["found"] = false;
        out["error"] = "empty_login";
        return out;
    }
    std::string lo = login;
    std::transform(lo.begin(), lo.end(), lo.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const std::string cache_key = "twitch:pub_profile:" + lo;
    if (cache_) {
        auto c = cache_->get(cache_key);
        if (c) {
            auto cached = json::parse(*c, nullptr, false);
            if (!cached.is_discarded()) return cached;
        }
    }

    std::string url = "/helix/users?login=" + encode_query_component(lo);
    auto response = http_get(url);
    auto j = json::parse(response, nullptr, false);
    if (j.is_discarded() || !j.contains("data") || !j["data"].is_array() || j["data"].empty()) {
        out["found"] = false;
        if (cache_) {
            try { cache_->set(cache_key, out.dump(), 120); } catch (...) {}
        }
        return out;
    }

    auto& u = j["data"][0];
    const std::string id = u.value("id", "");
    json twitch = {
        {"id", id},
        {"login", u.value("login", "")},
        {"display_name", u.value("display_name", "")},
        {"description", u.value("description", "")},
        {"profile_image_url", u.value("profile_image_url", "")},
        {"offline_image_url", u.value("offline_image_url", "")},
        {"view_count", u.value("view_count", 0)},
        {"created_at", u.value("created_at", "")},
        {"broadcaster_type", u.value("broadcaster_type", "")},
        {"type", u.value("type", "")}
    };

    const std::string stream_url = "/helix/streams?user_id=" + id;
    auto sres = http_get(stream_url);
    auto sj = json::parse(sres, nullptr, false);
    bool is_live = false;
    if (!sj.is_discarded() && sj.contains("data") && sj["data"].is_array() && !sj["data"].empty()) {
        is_live = true;
        auto& s = sj["data"][0];
        std::string thumb = s.value("thumbnail_url", std::string{});
        auto p1 = thumb.find("{width}");
        if (p1 != std::string::npos) thumb.replace(p1, 7, "1280");
        auto p2 = thumb.find("{height}");
        if (p2 != std::string::npos) thumb.replace(p2, 8, "720");
        twitch["stream"] = {
            {"title", s.value("title", "")},
            {"game_name", s.value("game_name", "")},
            {"game_id", s.value("game_id", "")},
            {"viewer_count", s.value("viewer_count", 0)},
            {"started_at", s.value("started_at", "")},
            {"thumbnail_url", thumb},
            {"language", s.value("language", "")}
        };
    }
    twitch["is_live"] = is_live;

    const std::string ch_url = "/helix/channels?broadcaster_id=" + id;
    auto cres = http_get(ch_url);
    auto cj = json::parse(cres, nullptr, false);
    if (!cj.is_discarded() && cj.contains("data") && cj["data"].is_array() && !cj["data"].empty()) {
        auto& ch = cj["data"][0];
        twitch["channel"] = {
            {"broadcaster_language", ch.value("broadcaster_language", "")},
            {"game_name", ch.value("game_name", "")},
            {"game_id", ch.value("game_id", "")},
            {"title", ch.value("title", "")},
            {"delay", ch.value("delay", 0)}
        };
    }

    out["found"] = true;
    out["twitch"] = std::move(twitch);
    if (cache_) {
        try { cache_->set(cache_key, out.dump(), 300); } catch (...) {}
    }
    return out;
}

} // namespace clipstream::services
