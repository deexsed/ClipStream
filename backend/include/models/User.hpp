#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace clipstream::models {

enum class UserRole { User, Moderator, Admin, Vip };
enum class Badge { None, RisingStar, Pro, Legend };

struct User {
    uint64_t id{};
    uint64_t auth_version{};
    std::string username;
    std::string email;
    std::string password_hash;
    std::string avatar_url;
    std::string twitch_id;
    std::string twitch_access_token;
    std::string twitch_refresh_token;
    std::string twitch_token_expires_at;
    std::string oauth_validated_at;
    UserRole role{UserRole::User};
    int reputation{};
    Badge badge{Badge::None};
    /// Блокировка входа и активности (модерация)
    bool account_blocked{false};
    /// ISO-подобная строка из MySQL или пусто = нет ограничения по чату
    std::string chat_banned_until;
    bool chat_banned_permanently{false};
    std::string account_suspended_until;
    std::string created_at;
    std::string updated_at;
    /// Версия принятого комплекта юр. документов (синхрон с AuthController kLegalConsentVersion)
    std::string legal_consent_version;
    std::string legal_consent_at;
};

inline void to_json(nlohmann::json &j, const User &u) {
    j = {{"id", u.id},
         {"username", u.username},
         {"email", u.email},
         {"avatar_url", u.avatar_url},
         {"role", static_cast<int>(u.role)},
         {"reputation", u.reputation},
         {"badge", static_cast<int>(u.badge)},
         {"created_at", u.created_at}};
    j["account_blocked"] = u.account_blocked;
    j["account_suspended_until"] = u.account_suspended_until.empty()
                                       ? nlohmann::json(nullptr)
                                       : nlohmann::json(u.account_suspended_until);
    j["chat_banned_permanently"] = u.chat_banned_permanently;
    if (u.chat_banned_until.empty())
        j["chat_banned_until"] = nullptr;
    else
        j["chat_banned_until"] = u.chat_banned_until;
    if (u.legal_consent_version.empty())
        j["legal_consent_version"] = nullptr;
    else
        j["legal_consent_version"] = u.legal_consent_version;
}

} // namespace clipstream::models
