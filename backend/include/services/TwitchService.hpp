#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <mutex>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include "models/Clip.hpp"
#include "services/CacheService.hpp"

namespace clipstream::services {

struct FetchResult {
    std::vector<models::Clip> clips;
    std::string cursor;
};

struct OAuthTokenSet {
    std::string access_token;
    std::string refresh_token;
    int expires_in{0};
    std::string scope;
};

struct TwitchUser {
    std::string id;
    std::string login;
    std::string display_name;
    std::string email;
    std::string profile_image_url;
};

inline void to_json(nlohmann::json& j, const TwitchUser& u) {
    j = {
        {"id", u.id},
        {"login", u.login},
        {"display_name", u.display_name},
        {"email", u.email},
        {"profile_image_url", u.profile_image_url}
    };
}

inline void from_json(const nlohmann::json& j, TwitchUser& u) {
    u.id = j.value("id", "");
    u.login = j.value("login", "");
    u.display_name = j.value("display_name", "");
    u.email = j.value("email", "");
    u.profile_image_url = j.value("profile_image_url", "");
}

struct GameInfo {
    std::string twitch_id;
    std::string name;
    std::string box_art_url;
};

inline void to_json(nlohmann::json& j, const GameInfo& g) {
    j = {
        {"twitch_id", g.twitch_id},
        {"name", g.name},
        {"box_art_url", g.box_art_url}
    };
}

inline void from_json(const nlohmann::json& j, GameInfo& g) {
    g.twitch_id = j.value("twitch_id", "");
    g.name = j.value("name", "");
    g.box_art_url = j.value("box_art_url", "");
}

/// Клиент Helix. Лимиты: см. https://dev.twitch.tv/docs/api/guide#rate-limits
/// (типично 800 запросов/мин на client id + токен, bucket, заголовки Ratelimit-Limit / Remaining / Reset, 429).
class TwitchService {
public:
    TwitchService(const std::string& client_id, const std::string& client_secret, CacheService* cache = nullptr);

    bool authenticate();

    /// @param bypass_response_cache если true — не читать/писать Redis для этого запроса (нужно периодическому синку,
    ///        иначе ответы Helix кэшируются на 5 мин и реальные запросы к Twitch редки).
    FetchResult fetch_clips(const std::string& game_id, int count = 100,
                            const std::string& cursor = "",
                            const std::string& started_at = "",
                            const std::string& ended_at = "",
                            int min_views = 50,
                            bool bypass_response_cache = false);

    std::vector<GameInfo> fetch_top_games(int count = 20);
    std::optional<TwitchUser> fetch_user_by_login(const std::string& login);
    std::optional<TwitchUser> fetch_user_by_id(const std::string& id);
    std::vector<nlohmann::json> fetch_live_streams(const std::string& game_twitch_id = "", int count = 12,
                                                   const std::string& sort = "viewers",
                                                   const std::string& language = "");
    std::vector<nlohmann::json> search_channels(const std::string& query, int count = 20);
    std::unordered_set<std::string> check_live_by_logins(const std::vector<std::string>& logins);
    std::string app_access_token();
    nlohmann::json helix_post_json(const std::string& url, const nlohmann::json& body);

    std::string get_auth_url(const std::string& redirect_uri) const;
    std::string exchange_code(const std::string& code, const std::string& redirect_uri);
    std::optional<OAuthTokenSet> exchange_code_tokens(const std::string& code, const std::string& redirect_uri);
    std::optional<OAuthTokenSet> refresh_user_token(const std::string& refresh_token);
    std::optional<TwitchUser> get_twitch_user(const std::string& user_token);
    std::vector<nlohmann::json> get_followed_channels(const std::string& user_token, const std::string& user_id, int count = 100);
    std::vector<nlohmann::json> get_followed_streams(const std::string& user_token, const std::string& user_id, int count = 100);
    std::vector<nlohmann::json> get_channels_info(const std::string& user_token, const std::vector<std::string>& broadcaster_ids);
    std::unordered_set<std::string> check_live_by_user_ids(const std::vector<std::string>& user_ids);

    /// Публичный профиль канала (app token): описание, баннер, стрим, Get Channel Information.
    nlohmann::json get_public_channel_profile(const std::string& login);

    const std::string& client_id() const { return client_id_; }

private:
    struct HttpResult {
        int status{0};
        std::string body;
        std::string retry_after;
        std::string ratelimit_limit;
        std::string ratelimit_remaining;
        std::string ratelimit_reset;
    };

    HttpResult https_request(const std::string& host, const std::string& target, const std::string& body,
                             bool is_post, const std::vector<std::pair<std::string, std::string>>& headers);
    void wait_for_rate_slot();
    /// После ответа api.twitch.tv: если Remaining == 0 — ждём до Ratelimit-Reset (epoch).
    static void apply_helix_ratelimit_headers(const HttpResult& res);
    bool ensure_app_token();

    std::string http_get(const std::string& url);
    std::string http_get_with_token(const std::string& url, const std::string& token);
    std::string http_post(const std::string& url, const std::string& body);
    std::string http_post_api(const std::string& url, const std::string& body);

    std::string client_id_;
    std::string client_secret_;
    std::string access_token_;
    std::chrono::steady_clock::time_point token_expires_at_{};
    std::chrono::steady_clock::time_point last_request_at_{};
    std::mutex token_mutex_;
    std::mutex request_mutex_;
    CacheService* cache_{nullptr};
};

} // namespace clipstream::services
