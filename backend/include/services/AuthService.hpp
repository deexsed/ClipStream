#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>
#include "models/User.hpp"
#include "repositories/UserRepository.hpp"

namespace clipstream::services {

struct AuthResult {
    std::string token;
    models::User user;
};

class AuthService {
public:
    AuthService(repositories::UserRepository& user_repo, const std::string& jwt_secret, int expiration_hours);

    std::optional<AuthResult> register_user(const std::string& username, const std::string& email, const std::string& password);
    std::optional<AuthResult> login(const std::string& email, const std::string& password);
    std::optional<AuthResult> login_twitch(const std::string& twitch_id, const std::string& username,
                                            const std::string& email, const std::string& avatar_url);
    std::optional<models::User> get_user(uint64_t user_id);
    nlohmann::json get_user_profile(uint64_t user_id);
    bool save_twitch_oauth(uint64_t user_id, const std::string& access_token,
                           const std::string& refresh_token,
                           const std::string& expires_at_iso);
    bool clear_twitch_oauth_by_twitch_id(const std::string& twitch_id);
    bool record_legal_consent(uint64_t user_id, const std::string& version, const std::string& accepted_at_iso);
    std::optional<uint64_t>   verify_token(const std::string& token);
    std::string               hash_password(const std::string& password);
    bool                      check_password(const std::string& password, const std::string& hash);

private:
    std::string generate_token(uint64_t user_id);

    repositories::UserRepository& user_repo_;
    std::string jwt_secret_;
    int expiration_hours_;
};

} // namespace clipstream::services
