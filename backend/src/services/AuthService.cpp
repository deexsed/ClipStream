#include "services/AuthService.hpp"
#include "services/CacheService.hpp"
#include "services/TwitchService.hpp"
#include "utils/Crypto.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <jwt-cpp/jwt.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sstream>

namespace clipstream::services {
namespace {
std::string token_key(const std::string &token);
std::string utc_after(int seconds, bool mysql_format);
} // namespace

AuthService::AuthService(repositories::UserRepository &user_repo, const std::string &jwt_secret,
                         int expiration_hours)
    : user_repo_(user_repo), jwt_secret_(jwt_secret), expiration_hours_(expiration_hours) {}

std::string AuthService::generate_token(uint64_t user_id) {
    auto now = std::chrono::system_clock::now();
    auto user = user_repo_.find_by_id(user_id);
    if (!user)
        throw std::runtime_error("User not found");
    return jwt::create()
        .set_issuer("clipstream")
        .set_subject(std::to_string(user_id))
        .set_payload_claim("version", jwt::claim(std::to_string(user->auth_version)))
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::hours(expiration_hours_))
        .sign(jwt::algorithm::hs256{jwt_secret_});
}

std::optional<uint64_t> AuthService::verify_token(const std::string &token) {
    uint64_t id = 0;
    std::string version;
    try {
        auto decoded = jwt::decode(token);
        auto verifier =
            jwt::verify().allow_algorithm(jwt::algorithm::hs256{jwt_secret_}).with_issuer("clipstream");
        verifier.verify(decoded);
        if (!decoded.has_expires_at() || !decoded.has_payload_claim("version"))
            return std::nullopt;
        id = std::stoull(decoded.get_subject());
        version = decoded.get_payload_claim("version").as_string();
    } catch (...) {
        return std::nullopt;
    }
    auto user = user_repo_.find_by_id(id);
    if (!user || user->twitch_id.empty() || user->account_blocked ||
        (!user->account_suspended_until.empty() && user->account_suspended_until > utc_after(0, true)) ||
        user->twitch_access_token.empty() || version != std::to_string(user->auth_version))
        return std::nullopt;
    // Infrastructure failures propagate as retryable 503, never as an expired local session.
    if (twitch_) {
        try {
            user_access_token(id);
        } catch (const TwitchError &error) {
            if (error.status == 401)
                return std::nullopt;
            throw;
        }
    }
    return id;
}

std::optional<models::User> AuthService::get_user(uint64_t user_id) { return user_repo_.find_by_id(user_id); }

nlohmann::json AuthService::get_user_profile(uint64_t user_id) {
    auto user = user_repo_.find_by_id(user_id);
    if (!user)
        return {{"error", "User not found"}};

    auto stats = user_repo_.get_profile_stats(user_id);
    const int liked = stats.value("liked_count", 0);
    const int viewed_unique = stats.value("unique_viewed_clips", 0);
    const int comments = stats.value("comments_count", 0);
    const int friends = stats.value("friends_count", 0);
    const int night_views = stats.value("night_views", 0);
    const int wall_posts = stats.value("wall_posts_count", 0);

    models::Badge computed_badge = models::Badge::None;
    if (viewed_unique >= 1000 || (liked >= 300 && comments >= 120))
        computed_badge = models::Badge::Legend;
    else if (viewed_unique >= 450 || (liked >= 140 && comments >= 40))
        computed_badge = models::Badge::Pro;
    else if (viewed_unique >= 100 || liked >= 30)
        computed_badge = models::Badge::RisingStar;

    if (user->badge != computed_badge) {
        user_repo_.update_badge(user_id, computed_badge);
        user = user_repo_.find_by_id(user_id);
    }

    auto badge_item = [](const std::string &id, const std::string &title, const std::string &description,
                         bool unlocked, int progress, int target, const std::string &color) {
        return nlohmann::json{{"id", id},
                              {"title", title},
                              {"description", description},
                              {"unlocked", unlocked},
                              {"progress", progress},
                              {"target", target},
                              {"color", color}};
    };

    nlohmann::json badges = nlohmann::json::array();
    badges.push_back(badge_item("first_clip", "Первые шаги", "Посмотреть первый клип", viewed_unique >= 1,
                                viewed_unique, 1, "gray"));
    badges.push_back(badge_item("explorer", "Исследователь", "Посмотреть 50 разных клипов",
                                viewed_unique >= 50, viewed_unique, 50, "blue"));
    badges.push_back(badge_item("rising_star", "Rising Star", "100 разных клипов или 30 лайков",
                                viewed_unique >= 100 || liked >= 30, std::max(viewed_unique, liked), 100,
                                "violet"));
    badges.push_back(badge_item("critic", "Критик", "Оставить 100 реакций (лайк/дизлайк)",
                                liked + stats.value("disliked_count", 0) >= 100,
                                liked + stats.value("disliked_count", 0), 100, "orange"));
    badges.push_back(badge_item("talker", "Комментатор", "Написать 20 комментариев", comments >= 20, comments,
                                20, "emerald"));
    badges.push_back(badge_item("first_friend", "Знакомство", "Первый друг на ClipStream", friends >= 1,
                                friends, 1, "sky"));
    badges.push_back(badge_item("social_circle", "Круг друзей", "10 друзей на ClipStream", friends >= 10,
                                friends, 10, "cyan"));
    badges.push_back(
        badge_item("social_star", "Свой человек", "25 друзей", friends >= 25, friends, 25, "violet"));
    badges.push_back(badge_item("wall_starter", "Стена жива", "Первая запись на стене профиля",
                                wall_posts >= 1, wall_posts, 1, "fuchsia"));
    badges.push_back(badge_item("night_owl", "Night Owl", "30 ночных просмотров (00:00-05:59)",
                                night_views >= 30, night_views, 30, "indigo"));
    badges.push_back(badge_item("legend", "Legend", "1000 разных клипов или 300 лайков + 120 комментариев",
                                viewed_unique >= 1000 || (liked >= 300 && comments >= 120),
                                std::max(viewed_unique, liked), 1000, "gold"));

    std::vector<std::string> sync_keys;
    for (const auto &b : badges) {
        if (b.value("unlocked", false)) {
            std::string kid = b.value("id", "");
            if (!kid.empty())
                sync_keys.push_back(kid);
        }
    }
    user_repo_.sync_earned_achievements(user_id, sync_keys);

    return {{"user", user ? nlohmann::json(*user) : nlohmann::json::object()},
            {"stats", stats},
            {"badges", badges}};
}

bool AuthService::save_twitch_oauth(uint64_t user_id, const std::string &access_token,
                                    const std::string &refresh_token, const std::string &expires_at_iso) {
    if (cache_) {
        auto old = user_repo_.find_by_id(user_id);
        if (old && !old->twitch_access_token.empty())
            cache_->set_required(token_key(old->twitch_access_token), std::to_string(user_id), 86400);
    }
    return user_repo_.update_twitch_oauth(user_id, access_token, refresh_token, expires_at_iso);
}

bool AuthService::clear_twitch_oauth_by_twitch_id(const std::string &twitch_id) {
    return user_repo_.clear_twitch_oauth_by_twitch_id(twitch_id);
}

bool AuthService::record_legal_consent(uint64_t user_id, const std::string &version,
                                       const std::string &accepted_at_iso) {
    return user_repo_.set_legal_consent(user_id, version, accepted_at_iso);
}

std::optional<AuthResult> AuthService::login_twitch(const std::string &twitch_id, const std::string &username,
                                                    const std::string &email, const std::string &avatar_url) {
    auto existing = user_repo_.find_by_twitch_id(twitch_id);
    if (existing) {
        existing->avatar_url = avatar_url;
        existing->username = username;
        try {
            user_repo_.update(*existing);
        } catch (const database::Error &error) {
            if (error.code == 1062)
                return std::nullopt;
            throw;
        }
        auto fresh = user_repo_.find_by_id(existing->id);
        if (!fresh || fresh->account_blocked ||
            (!fresh->account_suspended_until.empty() && fresh->account_suspended_until > utc_after(0, true)))
            return std::nullopt;
        return AuthResult{generate_token(fresh->id), *fresh};
    }

    models::User user;
    user.username = username;
    user.email = email.empty() ? (username + "@twitch.tv") : email;
    user.password_hash = "!oauth-only";
    user.avatar_url = avatar_url;
    user.twitch_id = twitch_id;

    try {
        uint64_t id = user_repo_.create(user);
        auto created = user_repo_.find_by_id(id);
        if (!created)
            return std::nullopt;
        return AuthResult{generate_token(id), *created};
    } catch (const database::Error &e) {
        // Never merge identities by unverified email or a display-name collision.
        if (e.code != 1062)
            throw;
        return std::nullopt;
    }
}

std::optional<nlohmann::json> AuthService::site_user_by_twitch_id(const std::string &twitch_id) {
    return user_repo_.find_site_user_by_twitch_id(twitch_id);
}

namespace {
std::string utc_after(int seconds, bool mysql_format = false) {
    auto t = std::time(nullptr) + seconds;
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, mysql_format ? "%Y-%m-%d %H:%M:%S" : "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
std::string token_key(const std::string &token) {
    unsigned char digest[32];
    SHA256(reinterpret_cast<const unsigned char *>(token.data()), token.size(), digest);
    return "oauth:token:" + utils::hex(digest, 32);
}
} // namespace
std::string AuthService::renew_token(uint64_t id) { return generate_token(id); }
void AuthService::revoke_sessions(uint64_t id) { user_repo_.revoke_sessions(id); }
AuthService::~AuthService() { stop_validation(); }
void AuthService::bind_twitch(TwitchService &twitch, CacheService &cache) {
    twitch_ = &twitch;
    cache_ = &cache;
}
void AuthService::start_validation() {
    if (validation_thread_.joinable())
        return;
    validation_thread_ = std::jthread([this](std::stop_token stop) {
        uint64_t cursor = 0;
        while (!stop.stop_requested()) {
            unsigned delay = 1;
            bool quota = false;
            try {
                auto users = user_repo_.oauth_users_for_validation(cursor, 100);
                if (users.empty()) {
                    cursor = 0;
                    delay = 60;
                }
                for (const auto &user : users) {
                    if (stop.stop_requested())
                        break;
                    try {
                        user_access_token(user.id);
                        cursor = user.id;
                    } catch (const TwitchError &error) {
                        if (error.status == 429) {
                            quota = true;
                            delay = static_cast<unsigned>(std::clamp(error.retry_after, 1, 60));
                            break;
                        }
                        cursor = user.id;
                        if (error.status != 401)
                            std::cerr << "[OAuth] validation deferred status=" << error.status << "\n";
                    }
                }
                if (cache_)
                    cache_->set_required("oauth:validation:worker",
                                         nlohmann::json{{"cursor", cursor},
                                                        {"rate_limited", quota},
                                                        {"updated_at", utc_after(0, true)}}
                                             .dump(),
                                         120);
            } catch (const std::exception &) {
                delay = 60;
                std::cerr << "[OAuth] validation batch deferred\n";
            }
            std::unique_lock lock(validation_mutex_);
            validation_cv_.wait_for(lock, std::chrono::seconds(delay), [&] { return stop.stop_requested(); });
        }
    });
}
void AuthService::stop_validation() {
    if (validation_thread_.joinable()) {
        validation_thread_.request_stop();
        validation_cv_.notify_all();
        validation_thread_.join();
    }
}
std::string AuthService::user_access_token(uint64_t id) { return refresh_or_validate(id, false); }
std::string AuthService::refresh_or_validate(uint64_t id, bool force) {
    if (!twitch_ || !cache_)
        throw std::runtime_error("OAuth provider not initialized");
    auto current = user_repo_.find_by_id(id);
    if (!current || current->account_blocked ||
        (!current->account_suspended_until.empty() &&
         current->account_suspended_until > utc_after(0, true)) ||
        current->twitch_access_token.empty())
        throw TwitchError(401, "OAuth session revoked", 0);
    if (!force && !current->oauth_validated_at.empty() &&
        current->oauth_validated_at > utc_after(-3600, true) &&
        current->twitch_token_expires_at > utc_after(60))
        return current->twitch_access_token;
    std::unique_lock local(refresh_mutexes_[id % refresh_mutexes_.size()], std::try_to_lock);
    if (!local.owns_lock())
        throw TwitchError(503, "OAuth validation in progress", 1);
    const auto key = "oauth:refresh:" + std::to_string(id);
    unsigned char random[16];
    if (RAND_bytes(random, 16) != 1)
        throw std::runtime_error("Random generator unavailable");
    auto lease = utils::hex(random, 16);
    if (!cache_->try_set_nx(key, lease, 90))
        throw TwitchError(503, "OAuth refresh in progress", 1);
    struct Release {
        CacheService *cache;
        std::string key, value;
        ~Release() {
            try {
                cache->compare_delete(key, value);
            } catch (...) {
            }
        }
    } release{cache_, key, lease};
    auto user = user_repo_.find_by_id(id);
    if (!user || user->account_blocked ||
        (!user->account_suspended_until.empty() && user->account_suspended_until > utc_after(0, true)) ||
        user->twitch_access_token.empty())
        throw TwitchError(401, "OAuth session revoked", 0);
    if (force || user->twitch_token_expires_at.empty() || user->twitch_token_expires_at <= utc_after(60)) {
        auto next = twitch_->refresh_user_token(user->twitch_refresh_token);
        if (!next) {
            user_repo_.clear_twitch_oauth_by_twitch_id(user->twitch_id);
            throw TwitchError(401, "Twitch authorization expired; sign in again", 0);
        }
        save_twitch_oauth(id, next->access_token, next->refresh_token,
                          utc_after(std::max(60, next->expires_in - 60)));
        user = user_repo_.find_by_id(id);
    }
    if (force || user->oauth_validated_at.empty() || user->oauth_validated_at <= utc_after(-3600, true)) {
        auto valid = twitch_->validate_user_token(user->twitch_access_token);
        if (!valid || valid->client_id != twitch_->client_id() || valid->user_id != user->twitch_id) {
            user_repo_.clear_twitch_oauth_by_twitch_id(user->twitch_id);
            throw TwitchError(401, "Twitch authorization revoked", 0);
        }
        user_repo_.mark_oauth_validated(id);
    }
    cache_->set_required(token_key(user->twitch_access_token), std::to_string(id), 86400);
    return user->twitch_access_token;
}
std::string AuthService::resolve_user_token(const std::string &token) {
    auto user = user_repo_.find_by_access_token(token);
    if (user)
        return refresh_or_validate(user->id, true);
    if (cache_) {
        auto id = cache_->get(token_key(token));
        if (id)
            return refresh_or_validate(std::stoull(*id), false);
    }
    throw TwitchError(401, "Twitch authorization revoked", 0);
}
std::string AuthService::resolve_user_token_by_twitch_id(const std::string &id) {
    auto user = user_repo_.find_by_twitch_id(id);
    if (!user)
        throw TwitchError(401, "EventSub user not linked", 0);
    return user_access_token(user->id);
}
} // namespace clipstream::services
