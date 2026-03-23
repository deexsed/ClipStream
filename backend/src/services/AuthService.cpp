#include "services/AuthService.hpp"
#include <jwt-cpp/jwt.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace clipstream::services {

AuthService::AuthService(repositories::UserRepository& user_repo,
                         const std::string& jwt_secret, int expiration_hours)
    : user_repo_(user_repo), jwt_secret_(jwt_secret), expiration_hours_(expiration_hours) {}

std::string AuthService::hash_password(const std::string& password) {
    unsigned char salt[16];
    RAND_bytes(salt, sizeof(salt));

    std::string salted;
    salted.assign(reinterpret_cast<char*>(salt), 16);
    salted += password;

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(salted.data()), salted.size(), hash);

    std::ostringstream oss;
    for (auto b : salt)  oss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
    oss << "$";
    for (auto b : hash)  oss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
    return oss.str();
}

bool AuthService::check_password(const std::string& password, const std::string& stored) {
    auto sep = stored.find('$');
    if (sep == std::string::npos || sep != 32) return false;

    std::string salt_hex = stored.substr(0, 32);
    std::string hash_hex = stored.substr(33);

    unsigned char salt[16];
    for (int i = 0; i < 16; ++i)
        salt[i] = static_cast<unsigned char>(std::stoi(salt_hex.substr(i * 2, 2), nullptr, 16));

    std::string salted;
    salted.assign(reinterpret_cast<char*>(salt), 16);
    salted += password;

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(salted.data()), salted.size(), hash);

    std::ostringstream oss;
    for (auto b : hash) oss << std::hex << std::setfill('0') << std::setw(2) << (int)b;

    return oss.str() == hash_hex;
}

std::string AuthService::generate_token(uint64_t user_id) {
    auto now = std::chrono::system_clock::now();
    return jwt::create()
        .set_issuer("clipstream")
        .set_subject(std::to_string(user_id))
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::hours(expiration_hours_))
        .sign(jwt::algorithm::hs256{jwt_secret_});
}

std::optional<uint64_t> AuthService::verify_token(const std::string& token) {
    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{jwt_secret_})
            .with_issuer("clipstream");
        verifier.verify(decoded);
        return std::stoull(decoded.get_subject());
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<AuthResult> AuthService::register_user(const std::string& username,
                                                      const std::string& email,
                                                      const std::string& password) {
    if (user_repo_.find_by_email(email)) return std::nullopt;
    if (user_repo_.find_by_username(username)) return std::nullopt;

    models::User user;
    user.username = username;
    user.email = email;
    user.password_hash = hash_password(password);

    uint64_t id = user_repo_.create(user);
    auto created = user_repo_.find_by_id(id);
    if (!created) return std::nullopt;

    return AuthResult{generate_token(id), *created};
}

std::optional<AuthResult> AuthService::login(const std::string& email, const std::string& password) {
    auto user = user_repo_.find_by_email(email);
    if (!user) return std::nullopt;
    if (!check_password(password, user->password_hash)) return std::nullopt;
    if (user->account_blocked) return std::nullopt;

    return AuthResult{generate_token(user->id), *user};
}

std::optional<models::User> AuthService::get_user(uint64_t user_id) {
    return user_repo_.find_by_id(user_id);
}

nlohmann::json AuthService::get_user_profile(uint64_t user_id) {
    auto user = user_repo_.find_by_id(user_id);
    if (!user) return {{"error", "User not found"}};

    auto stats = user_repo_.get_profile_stats(user_id);
    const int liked = stats.value("liked_count", 0);
    const int viewed_unique = stats.value("unique_viewed_clips", 0);
    const int comments = stats.value("comments_count", 0);
    const int friends     = stats.value("friends_count", 0);
    const int night_views = stats.value("night_views", 0);
    const int wall_posts  = stats.value("wall_posts_count", 0);

    models::Badge computed_badge = models::Badge::None;
    if (viewed_unique >= 1000 || (liked >= 300 && comments >= 120)) computed_badge = models::Badge::Legend;
    else if (viewed_unique >= 450 || (liked >= 140 && comments >= 40)) computed_badge = models::Badge::Pro;
    else if (viewed_unique >= 100 || liked >= 30) computed_badge = models::Badge::RisingStar;

    if (user->badge != computed_badge) {
        user_repo_.update_badge(user_id, computed_badge);
        user = user_repo_.find_by_id(user_id);
    }

    auto badge_item = [](const std::string& id, const std::string& title, const std::string& description,
                         bool unlocked, int progress, int target, const std::string& color) {
        return nlohmann::json{
            {"id", id},
            {"title", title},
            {"description", description},
            {"unlocked", unlocked},
            {"progress", progress},
            {"target", target},
            {"color", color}
        };
    };

    nlohmann::json badges = nlohmann::json::array();
    badges.push_back(badge_item("first_clip", "Первые шаги", "Посмотреть первый клип", viewed_unique >= 1, viewed_unique, 1, "gray"));
    badges.push_back(badge_item("explorer", "Исследователь", "Посмотреть 50 разных клипов", viewed_unique >= 50, viewed_unique, 50, "blue"));
    badges.push_back(badge_item("rising_star", "Rising Star", "100 разных клипов или 30 лайков", viewed_unique >= 100 || liked >= 30, std::max(viewed_unique, liked), 100, "violet"));
    badges.push_back(badge_item("critic", "Критик", "Оставить 100 реакций (лайк/дизлайк)", liked + stats.value("disliked_count", 0) >= 100, liked + stats.value("disliked_count", 0), 100, "orange"));
    badges.push_back(badge_item("talker", "Комментатор", "Написать 20 комментариев", comments >= 20, comments, 20, "emerald"));
    badges.push_back(badge_item("first_friend", "Знакомство", "Первый друг на ClipStream", friends >= 1, friends, 1, "sky"));
    badges.push_back(badge_item("social_circle", "Круг друзей", "10 друзей на ClipStream", friends >= 10, friends, 10, "cyan"));
    badges.push_back(badge_item("social_star", "Свой человек", "25 друзей", friends >= 25, friends, 25, "violet"));
    badges.push_back(badge_item("wall_starter", "Стена жива", "Первая запись на стене профиля", wall_posts >= 1, wall_posts, 1, "fuchsia"));
    badges.push_back(badge_item("night_owl", "Night Owl", "30 ночных просмотров (00:00-05:59)", night_views >= 30, night_views, 30, "indigo"));
    badges.push_back(badge_item("legend", "Legend", "1000 разных клипов или 300 лайков + 120 комментариев",
                                viewed_unique >= 1000 || (liked >= 300 && comments >= 120),
                                std::max(viewed_unique, liked), 1000, "gold"));

    std::vector<std::string> sync_keys;
    for (const auto& b : badges) {
        if (b.value("unlocked", false)) {
            std::string kid = b.value("id", "");
            if (!kid.empty()) sync_keys.push_back(kid);
        }
    }
    user_repo_.sync_earned_achievements(user_id, sync_keys);

    return {
        {"user", user ? nlohmann::json(*user) : nlohmann::json::object()},
        {"stats", stats},
        {"badges", badges}
    };
}

bool AuthService::save_twitch_oauth(uint64_t user_id, const std::string& access_token,
                                    const std::string& refresh_token,
                                    const std::string& expires_at_iso) {
    return user_repo_.update_twitch_oauth(user_id, access_token, refresh_token, expires_at_iso);
}

bool AuthService::clear_twitch_oauth_by_twitch_id(const std::string& twitch_id) {
    return user_repo_.clear_twitch_oauth_by_twitch_id(twitch_id);
}

bool AuthService::record_legal_consent(uint64_t user_id, const std::string& version, const std::string& accepted_at_iso) {
    return user_repo_.set_legal_consent(user_id, version, accepted_at_iso);
}

std::optional<AuthResult> AuthService::login_twitch(const std::string& twitch_id,
                                                     const std::string& username,
                                                     const std::string& email,
                                                     const std::string& avatar_url) {
    auto existing = user_repo_.find_by_twitch_id(twitch_id);
    if (existing) {
        existing->avatar_url = avatar_url;
        existing->username = username;
        user_repo_.update(*existing);
        auto fresh = user_repo_.find_by_id(existing->id);
        if (!fresh || fresh->account_blocked) return std::nullopt;
        return AuthResult{generate_token(fresh->id), *fresh};
    }

    models::User user;
    user.username = username;
    user.email = email.empty() ? (username + "@twitch.tv") : email;
    user.password_hash = hash_password(twitch_id + "_twitch_oauth");
    user.avatar_url = avatar_url;
    user.twitch_id = twitch_id;

    try {
        uint64_t id = user_repo_.create(user);
        auto created = user_repo_.find_by_id(id);
        if (!created) return std::nullopt;
        return AuthResult{generate_token(id), *created};
    } catch (...) {
        auto byEmail = user_repo_.find_by_email(user.email);
        if (byEmail) {
            if (byEmail->account_blocked) return std::nullopt;
            return AuthResult{generate_token(byEmail->id), *byEmail};
        }
        return std::nullopt;
    }
}

std::optional<nlohmann::json> AuthService::site_user_by_twitch_id(const std::string& twitch_id) {
    return user_repo_.find_site_user_by_twitch_id(twitch_id);
}

} // namespace clipstream::services
