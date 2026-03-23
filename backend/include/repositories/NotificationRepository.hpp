#pragma once

#include "database/ConnectionPool.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace clipstream::repositories {

class NotificationRepository {
public:
    explicit NotificationRepository(database::ConnectionPool& pool);

    void notify_comment_reply(uint64_t recipient_id, uint64_t actor_id, uint64_t clip_id, uint64_t parent_comment_id,
                              uint64_t reply_comment_id, const std::string& body_snippet);
    void notify_new_follower(uint64_t recipient_id, uint64_t follower_id);
    void notify_friend_request(uint64_t recipient_id, uint64_t requester_id);
    void notify_friend_accepted(uint64_t recipient_id, uint64_t accepter_id);
    void notify_wall_mention(uint64_t recipient_id, uint64_t actor_id, uint64_t post_id,
                             const std::string& owner_username);

    uint32_t                    unread_count(uint64_t user_id);
    std::vector<nlohmann::json> list_for_user(uint64_t user_id, uint32_t limit, uint32_t offset);
    void                        mark_read(uint64_t user_id, const std::vector<uint64_t>& ids);
    void                        mark_all_read(uint64_t user_id);

private:
    void insert_row(uint64_t recipient_id, const std::string& type, uint64_t actor_id, const nlohmann::json& payload);

    database::ConnectionPool& pool_;
};

} // namespace clipstream::repositories
