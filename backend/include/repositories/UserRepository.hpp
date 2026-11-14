#pragma once

#include "database/ConnectionPool.hpp"
#include "models/Clip.hpp"
#include "models/User.hpp"
#include "repositories/IRepository.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace clipstream::repositories {

class UserRepository : public IRepository<models::User> {
  public:
    explicit UserRepository(database::ConnectionPool &pool);

    std::optional<models::User> find_by_id(uint64_t id) override;
    std::vector<models::User> find_all(uint32_t limit, uint32_t offset) override;
    uint64_t create(const models::User &user) override;
    bool update(const models::User &user) override;
    bool remove(uint64_t id) override;

    std::optional<models::User> find_by_email(const std::string &email);
    std::optional<models::User> find_by_username(const std::string &username);
    std::optional<models::User> find_by_twitch_id(const std::string &twitch_id);
    std::optional<models::User> find_by_access_token(const std::string &token);
    std::vector<models::User> oauth_users_for_validation(uint64_t after_id, uint32_t limit);
    void update_password_hash(uint64_t id, const std::string &hash);
    void revoke_sessions(uint64_t id);
    void mark_oauth_validated(uint64_t user_id);
    /// id пользователя сайта, которому начисляется репутация за клип (author_id или twitch creator).
    uint64_t clip_owner_user_id_for_reputation(const models::Clip &clip);
    nlohmann::json get_profile_stats(uint64_t user_id);
    bool update_badge(uint64_t user_id, models::Badge badge);
    bool update_twitch_oauth(uint64_t id, const std::string &access_token, const std::string &refresh_token,
                             const std::string &expires_at_iso);
    bool clear_twitch_oauth_by_twitch_id(const std::string &twitch_id);
    bool set_legal_consent(uint64_t user_id, const std::string &version, const std::string &accepted_at_iso);

    uint32_t count_by_role(models::UserRole role);
    bool update_role(uint64_t user_id, models::UserRole role, uint64_t actor_id = 0);
    nlohmann::json get_admin_overview_stats();
    std::vector<nlohmann::json> list_users_admin(uint32_t limit, uint32_t offset, const std::string &search);
    uint64_t count_users_admin(const std::string &search);

    bool may_post_comment(uint64_t user_id);

    bool follow(uint64_t follower_id, uint64_t followed_id);
    /// 1 = новая подписка, 0 = уже были подписаны (или self), -1 = ошибка SQL.
    int try_follow(uint64_t follower_id, uint64_t followed_id);
    bool unfollow(uint64_t follower_id, uint64_t followed_id);
    std::unordered_set<uint64_t> following_ids_among(uint64_t follower_id,
                                                     const std::vector<uint64_t> &user_ids);
    bool is_following(uint64_t follower_id, uint64_t followed_id);
    nlohmann::json get_user_settings(uint64_t user_id);
    bool save_user_settings(uint64_t user_id, bool hide_mature, const std::vector<std::string> &mute_keywords,
                            const std::vector<std::string> &clip_languages);
    std::vector<nlohmann::json> get_badge_history(uint64_t user_id, uint32_t limit);
    std::optional<nlohmann::json> get_public_profile(const std::string &username, uint64_t viewer_id);
    /// Публичная привязка аккаунта сайта по Twitch user id (для редиректа /channel → /u).
    std::optional<nlohmann::json> find_site_user_by_twitch_id(const std::string &twitch_id);
    /// Активность в analytics_sessions за последние window_minutes (UTC).
    bool has_recent_site_activity(uint64_t user_id, unsigned window_minutes);

    std::vector<nlohmann::json> list_followers(uint64_t user_id, uint32_t limit, uint32_t offset);
    std::vector<nlohmann::json> list_following(uint64_t user_id, uint32_t limit, uint32_t offset);
    std::vector<nlohmann::json> search_users_by_username_prefix(const std::string &query, uint32_t limit);

    /// Статус дружбы viewer → target: none | friends | outgoing | incoming | self
    std::string friendship_status(uint64_t viewer_id, uint64_t target_id);
    std::unordered_map<uint64_t, std::string> friendship_status_bulk(uint64_t viewer_id,
                                                                     const std::vector<uint64_t> &target_ids);
    /// 1 = новая заявка, 2 = сразу стали друзьями (встречная заявка), 0 = уже друзья или заявка есть, -1 =
    /// ошибка
    int send_friend_request(uint64_t requester_id, uint64_t addressee_id);
    bool cancel_friend_request(uint64_t requester_id, uint64_t addressee_id);
    bool accept_friend_request(uint64_t addressee_id, uint64_t requester_id);
    bool decline_friend_request(uint64_t addressee_id, uint64_t requester_id);
    bool remove_friendship(uint64_t user_a, uint64_t user_b);
    std::vector<nlohmann::json> list_friends(uint64_t user_id, uint32_t limit, uint32_t offset);
    std::vector<nlohmann::json> list_friend_requests_incoming(uint64_t user_id, uint32_t limit,
                                                              uint32_t offset);
    std::vector<nlohmann::json> list_friend_requests_outgoing(uint64_t user_id, uint32_t limit,
                                                              uint32_t offset);

    bool sync_earned_achievements(uint64_t user_id, const std::vector<std::string> &keys);
    std::vector<nlohmann::json> list_earned_achievements(uint64_t user_id, uint32_t limit);

    std::vector<nlohmann::json> list_wall_posts(uint64_t profile_owner_id, uint32_t limit, uint32_t offset);
    std::optional<uint64_t> create_wall_post(uint64_t author_id, uint64_t profile_owner_id,
                                             const std::string &body);
    std::vector<uint64_t> wall_post_mentioned_user_ids(uint64_t post_id);

  private:
    models::User row_to_user(MYSQL_ROW row, MYSQL_RES *res);
    database::ConnectionPool &pool_;
};

} // namespace clipstream::repositories
