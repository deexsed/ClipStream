#include "repositories/NotificationRepository.hpp"
#include "database/SqlEscape.hpp"

namespace clipstream::repositories {

NotificationRepository::NotificationRepository(database::ConnectionPool &pool) : pool_(pool) {}

void NotificationRepository::insert_row(uint64_t recipient_id, const std::string &type, uint64_t actor_id,
                                        const nlohmann::json &payload) {
    if (recipient_id == 0 || actor_id == 0 || recipient_id == actor_id)
        return;
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string pj = payload.is_null() ? "NULL" : ("'" + esc(payload.dump()) + "'");
    std::string sql = "INSERT INTO notifications (user_id, type, actor_id, payload) VALUES (" +
                      std::to_string(recipient_id) + ",'" + esc(type) + "'," + std::to_string(actor_id) +
                      "," + pj + ")";
    database::query(conn.raw(), sql.c_str());
}

void NotificationRepository::notify_comment_reply(uint64_t recipient_id, uint64_t actor_id, uint64_t clip_id,
                                                  uint64_t parent_comment_id, uint64_t reply_comment_id,
                                                  const std::string &body_snippet) {
    nlohmann::json p = {{"clip_id", clip_id},
                        {"parent_comment_id", parent_comment_id},
                        {"reply_comment_id", reply_comment_id},
                        {"snippet", body_snippet}};
    insert_row(recipient_id, "comment_reply", actor_id, p);
}

void NotificationRepository::notify_new_follower(uint64_t recipient_id, uint64_t follower_id) {
    insert_row(recipient_id, "new_follower", follower_id, nlohmann::json::object());
}

void NotificationRepository::notify_friend_request(uint64_t recipient_id, uint64_t requester_id) {
    insert_row(recipient_id, "friend_request", requester_id, nlohmann::json::object());
}

void NotificationRepository::notify_friend_accepted(uint64_t recipient_id, uint64_t accepter_id) {
    insert_row(recipient_id, "friend_accepted", accepter_id, nlohmann::json::object());
}

void NotificationRepository::notify_wall_mention(uint64_t recipient_id, uint64_t actor_id, uint64_t post_id,
                                                 const std::string &owner_username) {
    nlohmann::json p = {{"post_id", post_id}, {"profile_username", owner_username}};
    insert_row(recipient_id, "wall_mention", actor_id, p);
}

uint32_t NotificationRepository::unread_count(uint64_t user_id) {
    if (user_id == 0)
        return 0;
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT COUNT(*) FROM notifications WHERE user_id=" + std::to_string(user_id) +
                      " AND read_at IS NULL";
    if (database::query(conn.raw(), sql.c_str()))
        return 0;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    uint32_t n = 0;
    if (row && row[0])
        n = static_cast<uint32_t>(std::stoul(row[0]));
    res.reset();
    return n;
}

std::vector<nlohmann::json> NotificationRepository::list_for_user(uint64_t user_id, uint32_t limit,
                                                                  uint32_t offset) {
    std::vector<nlohmann::json> out;
    if (user_id == 0)
        return out;
    if (limit > 100)
        limit = 100;
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT n.id, n.type, n.actor_id, n.payload, n.read_at, n.created_at, u.username AS actor_username, "
        "u.avatar_url AS actor_avatar "
        "FROM notifications n JOIN users u ON u.id=n.actor_id "
        "WHERE n.user_id=" +
        std::to_string(user_id) + " ORDER BY n.created_at DESC LIMIT " + std::to_string(limit) + " OFFSET " +
        std::to_string(offset);
    if (database::query(conn.raw(), sql.c_str()))
        return out;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        nlohmann::json j;
        j["id"] = row[0] ? std::stoull(row[0]) : 0;
        j["type"] = row[1] ? row[1] : "";
        j["actor_id"] = row[2] ? std::stoull(row[2]) : 0;
        if (row[3] && row[3][0]) {
            try {
                j["payload"] = nlohmann::json::parse(row[3]);
            } catch (...) {
                j["payload"] = nlohmann::json::object();
            }
        } else
            j["payload"] = nlohmann::json::object();
        j["read"] = (row[4] != nullptr && row[4][0] != 0);
        j["created_at"] = row[5] ? row[5] : "";
        j["actor_username"] = row[6] ? row[6] : "";
        j["actor_avatar"] = row[7] ? row[7] : "";
        out.push_back(std::move(j));
    }
    res.reset();
    return out;
}

void NotificationRepository::mark_read(uint64_t user_id, const std::vector<uint64_t> &ids) {
    if (user_id == 0 || ids.empty())
        return;
    database::ScopedConnection conn(pool_);
    std::ostringstream in;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i)
            in << ",";
        in << ids[i];
    }
    std::string sql =
        "UPDATE notifications SET read_at=UTC_TIMESTAMP() WHERE user_id=" + std::to_string(user_id) +
        " AND id IN (" + in.str() + ") AND read_at IS NULL";
    database::query(conn.raw(), sql.c_str());
}

void NotificationRepository::mark_all_read(uint64_t user_id) {
    if (user_id == 0)
        return;
    database::ScopedConnection conn(pool_);
    std::string sql =
        "UPDATE notifications SET read_at=UTC_TIMESTAMP() WHERE user_id=" + std::to_string(user_id) +
        " AND read_at IS NULL";
    database::query(conn.raw(), sql.c_str());
}

} // namespace clipstream::repositories
