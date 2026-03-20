#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace clipstream::models {

struct Comment {
    uint64_t                id{};
    uint64_t                clip_id{};
    uint64_t                user_id{};
    std::optional<uint64_t> parent_id;
    std::string             body;
    std::string             created_at;
    std::string             updated_at;

    std::string             author_name;
    std::string             author_avatar;
    /// users.role: user | moderator | admin | vip
    std::string             author_role{"user"};
    /// users.badge: none | rising_star | pro | legend
    std::string             author_cosmetic_badge{"none"};
    std::string             author_twitch_id;
    bool                    is_clip_channel_owner{false};
    std::vector<std::string> author_badges;
    std::vector<Comment>    replies;
};

inline void to_json(nlohmann::json& j, const Comment& c) {
    j = {
        {"id",            c.id},
        {"clip_id",       c.clip_id},
        {"user_id",       c.user_id},
        {"body",          c.body},
        {"author_name",   c.author_name},
        {"author_avatar", c.author_avatar},
        {"author_role",   c.author_role},
        {"author_badges", c.author_badges},
        {"is_clip_channel_owner", c.is_clip_channel_owner},
        {"created_at",    c.created_at},
        {"updated_at",    c.updated_at},
        {"replies",       c.replies}
    };
    if (c.parent_id) j["parent_id"] = *c.parent_id;
}

} // namespace clipstream::models
