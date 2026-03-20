#pragma once

#include "repositories/IRepository.hpp"
#include "models/User.hpp"
#include "database/ConnectionPool.hpp"
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>

namespace clipstream::repositories {

class UserRepository : public IRepository<models::User> {
public:
    explicit UserRepository(database::ConnectionPool& pool);

    std::optional<models::User> find_by_id(uint64_t id) override;
    std::vector<models::User>   find_all(uint32_t limit, uint32_t offset) override;
    uint64_t                    create(const models::User& user) override;
    bool                        update(const models::User& user) override;
    bool                        remove(uint64_t id) override;

    std::optional<models::User> find_by_email(const std::string& email);
    std::optional<models::User> find_by_username(const std::string& username);
    std::optional<models::User> find_by_twitch_id(const std::string& twitch_id);
    bool                        update_reputation(uint64_t id, int delta);
    nlohmann::json              get_profile_stats(uint64_t user_id);
    bool                        update_badge(uint64_t user_id, models::Badge badge);
    bool                        update_twitch_oauth(uint64_t id, const std::string& access_token,
                                                    const std::string& refresh_token,
                                                    const std::string& expires_at_iso);
    bool                        clear_twitch_oauth_by_twitch_id(const std::string& twitch_id);
    bool                        set_legal_consent(uint64_t user_id, const std::string& version,
                                                  const std::string& accepted_at_iso);

    uint32_t                    count_by_role(models::UserRole role);
    bool                        update_role(uint64_t user_id, models::UserRole role);
    nlohmann::json              get_admin_overview_stats();
    std::vector<nlohmann::json> list_users_admin(uint32_t limit, uint32_t offset, const std::string& search);
    uint64_t                    count_users_admin(const std::string& search);

    bool may_post_comment(uint64_t user_id);
    bool apply_chat_ban(uint64_t user_id, bool permanent, uint32_t days, const std::string& reason,
                        uint64_t moderator_id);
    bool clear_chat_ban(uint64_t user_id);
    bool set_account_blocked(uint64_t user_id, bool blocked);

    bool                        follow(uint64_t follower_id, uint64_t followed_id);
    bool                        unfollow(uint64_t follower_id, uint64_t followed_id);
    bool                        is_following(uint64_t follower_id, uint64_t followed_id);
    nlohmann::json              get_user_settings(uint64_t user_id);
    bool save_user_settings(uint64_t user_id, bool hide_mature, const std::vector<std::string>& mute_keywords,
                            const std::vector<std::string>& clip_languages);
    std::vector<nlohmann::json> get_badge_history(uint64_t user_id, uint32_t limit);
    std::optional<nlohmann::json> get_public_profile(const std::string& username, uint64_t viewer_id);

    std::vector<nlohmann::json> list_followers(uint64_t user_id, uint32_t limit, uint32_t offset);
    std::vector<nlohmann::json> list_following(uint64_t user_id, uint32_t limit, uint32_t offset);
    std::vector<nlohmann::json> search_users_by_username_prefix(const std::string& query, uint32_t limit);

private:
    models::User row_to_user(MYSQL_ROW row, MYSQL_RES* res);
    database::ConnectionPool& pool_;
};

} // namespace clipstream::repositories
