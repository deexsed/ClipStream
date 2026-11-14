#pragma once

#include "models/Clip.hpp"
#include "services/CacheService.hpp"
#include <chrono>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace clipstream::services {

class TwitchError : public std::runtime_error {
  public:
    const int status;
    const int retry_after;
    const int upstream_status;
    const bool history_budget;
    TwitchError(int code, const std::string& message, int retry = 0, int upstream = 0, bool history = false)
        : std::runtime_error(message), status(code), retry_after(retry), upstream_status(upstream),
          history_budget(history) {}
};

struct TokenValidation {
    std::string client_id;
    std::string user_id;
    int expires_in{};
    std::vector<std::string> scopes;
};

struct FetchResult {
    std::vector<models::Clip> clips;
    std::string cursor;
    unsigned raw_count{};
    bool below_threshold{};
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
    j = {{"id", u.id},
         {"login", u.login},
         {"display_name", u.display_name},
         {"email", u.email},
         {"profile_image_url", u.profile_image_url}};
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
    j = {{"twitch_id", g.twitch_id}, {"name", g.name}, {"box_art_url", g.box_art_url}};
}

inline void from_json(const nlohmann::json& j, GameInfo& g) {
    g.twitch_id = j.value("twitch_id", "");
    g.name = j.value("name", "");
    g.box_art_url = j.value("box_art_url", "");
}

/// Клиент Helix. Лимиты: см. https://dev.twitch.tv/docs/api/guide#rate-limits
/// (типично 800 запросов/мин на client id + токен, bucket, заголовки
/// Ratelimit-Limit / Remaining / Reset, 429).
struct GamePage {
    std::vector<GameInfo> games;
    std::string cursor;
};

class TwitchService {
  public:
    struct HttpResult {
        int status{0};
        std::string body;
        std::string retry_after;
        std::string ratelimit_limit;
        std::string ratelimit_remaining;
        std::string ratelimit_reset;
        std::string rate_bucket;
    };

    using HttpTransport =
        std::function<HttpResult(const std::string&, const std::string&, const std::string&, bool,
                                 const std::vector<std::pair<std::string, std::string>>&)>;
    TwitchService(const std::string& client_id, const std::string& client_secret,
                  CacheService* cache = nullptr, HttpTransport transport = {});

    // The scope marks every nested HTTP request (including enrichment) as
    // background work.
    class BackgroundScope {
      public:
        explicit BackgroundScope(unsigned& requests, bool history = false)
            : previous_(background_requests_), previous_history_(background_history_) {
            background_requests_ = &requests;
            background_history_ = history;
        }
        ~BackgroundScope() {
            background_requests_ = previous_;
            background_history_ = previous_history_;
        }
        BackgroundScope(const BackgroundScope&) = delete;
        BackgroundScope& operator=(const BackgroundScope&) = delete;

      private:
        unsigned* previous_;
        bool previous_history_;
    };
    nlohmann::json rate_status();
    GamePage fetch_top_games_page(int count, const std::string& cursor = "");
    bool authenticate();
    std::optional<TokenValidation> validate_user_token(const std::string& token);
    void set_user_token_refresh_handler(std::function<std::string(const std::string&)> handler);
    std::unordered_map<std::string, TwitchUser> fetch_users_by_ids(const std::vector<std::string>& ids);
    nlohmann::json helix_post_user_json(const std::string& url, const nlohmann::json& body,
                                        const std::string& token);

    /// @param bypass_response_cache если true — не читать/писать Redis для этого
    /// запроса (нужно периодическому синку,
    ///        иначе ответы Helix кэшируются на 5 мин и реальные запросы к Twitch
    ///        редки).
    FetchResult fetch_clips(const std::string& game_id, int count = 100, const std::string& cursor = "",
                            const std::string& started_at = "", const std::string& ended_at = "",
                            int min_views = 50, bool bypass_response_cache = false);

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
    std::optional<OAuthTokenSet> exchange_code_tokens(const std::string& code,
                                                      const std::string& redirect_uri);
    std::optional<OAuthTokenSet> refresh_user_token(const std::string& refresh_token);
    std::optional<TwitchUser> get_twitch_user(const std::string& user_token);
    std::vector<nlohmann::json> get_followed_channels(const std::string& user_token,
                                                      const std::string& user_id, int count = 100);
    std::vector<nlohmann::json> get_followed_streams(const std::string& user_token,
                                                     const std::string& user_id, int count = 100);
    std::vector<nlohmann::json> get_channels_info(const std::string& user_token,
                                                  const std::vector<std::string>& broadcaster_ids);
    std::unordered_set<std::string> check_live_by_user_ids(const std::vector<std::string>& user_ids);

    /// Публичный профиль канала (app token): описание, баннер, стрим, Get Channel
    /// Information.
    nlohmann::json get_public_channel_profile(const std::string& login);

    const std::string& client_id() const { return client_id_; }

  private:
    HttpResult https_request(const std::string& host, const std::string& target, const std::string& body,
                             bool is_post, const std::vector<std::pair<std::string, std::string>>& headers);
    void wait_for_rate_slot(const std::string& bucket);
    void apply_helix_ratelimit_headers(const HttpResult& res);
    void require_success(const HttpResult& result);
    bool authenticate_locked();
    bool ensure_app_token();

    std::string http_get(const std::string& url, bool bypass_cache = false);
    std::string http_get_with_token(const std::string& url, const std::string& token);
    std::string http_post(const std::string& url, const std::string& body);
    std::string http_post_api(const std::string& url, const std::string& body);

    static thread_local unsigned* background_requests_;
    static thread_local bool background_history_;
    std::string client_id_;
    std::string client_secret_;
    std::string access_token_;
    std::chrono::steady_clock::time_point token_expires_at_{};
    std::chrono::steady_clock::time_point last_request_at_{};
    std::mutex token_mutex_;
    std::mutex request_mutex_;
    CacheService* cache_{nullptr};
    std::function<std::string(const std::string&)> user_token_refresh_handler_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> rate_resets_;
    unsigned rate_count_{};
    HttpTransport transport_;
};

} // namespace clipstream::services
