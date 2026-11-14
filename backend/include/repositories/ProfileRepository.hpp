#pragma once
#include "database/ConnectionPool.hpp"
#include "models/User.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace clipstream::repositories {
struct ProfileEntity {
    std::string key;
    std::string twitch_id;
    uint64_t user_id{};
    std::string login;
    std::string display_name;
    std::string avatar_url;
    std::string username;
    int age_seconds{};
};
class ProfileRepository {
  public:
    explicit ProfileRepository(database::ConnectionPool &pool) : pool_(pool) {}
    std::optional<ProfileEntity> find(const std::string &key);
    ProfileEntity ensure_user(const models::User &user);
    ProfileEntity ensure_twitch(const std::string &id, const std::string &login, const std::string &display,
                                const std::string &avatar);
    bool follow(uint64_t viewer, const std::string &key);
    void unfollow(uint64_t viewer, const std::string &key);
    bool is_following(uint64_t viewer, const std::string &key);
    std::unordered_set<std::string> following_among(uint64_t viewer, const std::vector<ProfileEntity> &rows);
    std::vector<ProfileEntity> following(uint64_t user, uint32_t limit, uint32_t offset);
    std::vector<ProfileEntity> followers(const std::string &key, uint32_t limit, uint32_t offset);
    nlohmann::json stats(const ProfileEntity &entity);
    nlohmann::json settings(uint64_t user);
    void save_settings(uint64_t user, const nlohmann::json &settings);
    bool may_view(uint64_t owner, uint64_t viewer, const std::string &field = "profile_visibility");
    bool may_post_wall(uint64_t owner, uint64_t viewer);
    enum class WallMutation { Applied, NotFound, Forbidden };
    WallMutation mutate_wall(uint64_t post, uint64_t viewer, bool staff, bool remove,
                             const std::string &body);
    static nlohmann::json card(const ProfileEntity &entity);

  private:
    std::vector<ProfileEntity> select(const std::string &suffix);
    database::ConnectionPool &pool_;
};
} // namespace clipstream::repositories
