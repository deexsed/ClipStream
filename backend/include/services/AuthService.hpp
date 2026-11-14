#pragma once

#include "models/User.hpp"
#include "repositories/UserRepository.hpp"
#include <array>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>

namespace clipstream::services {
class TwitchService;
class CacheService;

struct AuthResult {
    std::string token;
    models::User user;
};

class AuthService {
  public:
    AuthService(repositories::UserRepository &user_repo, const std::string &jwt_secret, int expiration_hours);

    ~AuthService();
    std::string renew_token(uint64_t id);
    void revoke_sessions(uint64_t id);
    void bind_twitch(TwitchService &twitch, CacheService &cache);
    void start_validation();
    void stop_validation();
    std::string user_access_token(uint64_t user_id);
    std::string resolve_user_token(const std::string &current_token);
    std::string resolve_user_token_by_twitch_id(const std::string &twitch_id);
    std::optional<AuthResult> login_twitch(const std::string &twitch_id, const std::string &username,
                                           const std::string &email, const std::string &avatar_url);
    std::optional<models::User> get_user(uint64_t user_id);
    nlohmann::json get_user_profile(uint64_t user_id);
    bool save_twitch_oauth(uint64_t user_id, const std::string &access_token,
                           const std::string &refresh_token, const std::string &expires_at_iso);
    bool clear_twitch_oauth_by_twitch_id(const std::string &twitch_id);
    bool record_legal_consent(uint64_t user_id, const std::string &version,
                              const std::string &accepted_at_iso);
    std::optional<uint64_t> verify_token(const std::string &token);
    std::optional<nlohmann::json> site_user_by_twitch_id(const std::string &twitch_id);

  private:
    std::string refresh_or_validate(uint64_t user_id, bool force_refresh);
    std::string generate_token(uint64_t user_id);

    repositories::UserRepository &user_repo_;
    std::string jwt_secret_;
    int expiration_hours_;
    TwitchService *twitch_{nullptr};
    CacheService *cache_{nullptr};
    std::array<std::mutex, 64> refresh_mutexes_;
    std::jthread validation_thread_;
    std::mutex validation_mutex_;
    std::condition_variable validation_cv_;
};

} // namespace clipstream::services
