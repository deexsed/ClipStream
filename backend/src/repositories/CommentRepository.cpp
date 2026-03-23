#include "repositories/CommentRepository.hpp"
#include "database/SqlEscape.hpp"
#include <cstring>
#include <stdexcept>

namespace clipstream::repositories {
namespace {

std::string vote_join_sql(uint64_t viewer_id) {
    return std::string(
               "LEFT JOIN ("
               "SELECT comment_id, "
               "SUM(CASE WHEN vote=1 THEN 1 ELSE 0 END) AS vote_up, "
               "SUM(CASE WHEN vote=-1 THEN 1 ELSE 0 END) AS vote_down "
               "FROM comment_votes GROUP BY comment_id"
               ") va ON va.comment_id=c.id "
               "LEFT JOIN comment_votes myv ON myv.comment_id=c.id AND myv.user_id=")
        + std::to_string(viewer_id);
}

} // namespace

CommentRepository::CommentRepository(database::ConnectionPool& pool) : pool_(pool) {}

models::Comment CommentRepository::row_to_comment(MYSQL_ROW row, MYSQL_RES* res) {
    auto idx = [&](const char* name) -> int {
        unsigned int num = mysql_num_fields(res);
        MYSQL_FIELD* fields = mysql_fetch_fields(res);
        for (unsigned int i = 0; i < num; ++i)
            if (std::strcmp(fields[i].name, name) == 0) return static_cast<int>(i);
        return -1;
    };

    models::Comment c;
    c.id         = std::stoull(row[idx("id")]);
    c.clip_id    = std::stoull(row[idx("clip_id")]);
    c.user_id    = std::stoull(row[idx("user_id")]);
    c.body       = row[idx("body")]       ? row[idx("body")]       : "";
    c.created_at = row[idx("created_at")] ? row[idx("created_at")] : "";
    c.updated_at = row[idx("updated_at")] ? row[idx("updated_at")] : "";

    int pi = idx("parent_id");
    if (pi >= 0 && row[pi]) c.parent_id = std::stoull(row[pi]);

    int ai = idx("author_name");
    if (ai >= 0 && row[ai]) c.author_name = row[ai];

    int avi = idx("author_avatar");
    if (avi >= 0 && row[avi]) c.author_avatar = row[avi];

    int ar = idx("author_role");
    if (ar >= 0 && row[ar]) c.author_role = row[ar];
    else c.author_role = "user";

    int tw = idx("author_twitch_id");
    if (tw >= 0 && row[tw]) c.author_twitch_id = row[tw];

    int ub = idx("user_badge");
    if (ub >= 0 && row[ub] && row[ub][0]) c.author_cosmetic_badge = row[ub];
    else c.author_cosmetic_badge = "none";

    int vu = idx("cv_up");
    if (vu >= 0 && row[vu]) c.upvotes = static_cast<uint32_t>(std::stoul(row[vu]));
    int vd = idx("cv_down");
    if (vd >= 0 && row[vd]) c.downvotes = static_cast<uint32_t>(std::stoul(row[vd]));
    int mv = idx("cv_my_vote");
    if (mv >= 0 && row[mv] && row[mv][0]) c.user_vote = static_cast<int8_t>(std::stoi(row[mv]));

    return c;
}

std::optional<models::Comment> CommentRepository::find_by_id(uint64_t id) {
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT c.*, u.username AS author_name, u.avatar_url AS author_avatar, u.`role` AS author_role, "
        "u.twitch_id AS author_twitch_id, u.badge AS user_badge, "
        "COALESCE(va.vote_up,0) AS cv_up, COALESCE(va.vote_down,0) AS cv_down, NULL AS cv_my_vote "
        "FROM comments c JOIN users u ON c.user_id=u.id "
        "LEFT JOIN ("
        "SELECT comment_id, SUM(CASE WHEN vote=1 THEN 1 ELSE 0 END) AS vote_up, "
        "SUM(CASE WHEN vote=-1 THEN 1 ELSE 0 END) AS vote_down FROM comment_votes GROUP BY comment_id"
        ") va ON va.comment_id=c.id "
        "WHERE c.id=" + std::to_string(id);
    if (mysql_query(conn.raw(), sql.c_str())) return std::nullopt;

    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) { mysql_free_result(res); return std::nullopt; }

    auto comment = row_to_comment(row, res);
    mysql_free_result(res);
    return comment;
}

std::vector<models::Comment> CommentRepository::find_all(uint32_t limit, uint32_t offset) {
    return find_by_clip(0, limit, offset, "recent", 0);
}

std::vector<models::Comment> CommentRepository::find_by_clip(uint64_t clip_id, uint32_t limit, uint32_t offset,
                                                             const std::string& sort, uint64_t viewer_user_id) {
    database::ScopedConnection conn(pool_);
    const bool best = (sort == "best");
    std::string where_clip =
        clip_id != 0 ? ("c.clip_id=" + std::to_string(clip_id) + " AND ") : "";
    std::ostringstream sql;
    sql << "SELECT c.*, u.username AS author_name, u.avatar_url AS author_avatar, u.`role` AS author_role, "
        << "u.twitch_id AS author_twitch_id, u.badge AS user_badge, "
        << "COALESCE(va.vote_up,0) AS cv_up, COALESCE(va.vote_down,0) AS cv_down, myv.vote AS cv_my_vote "
        << "FROM comments c JOIN users u ON c.user_id=u.id " << vote_join_sql(viewer_user_id) << " "
        << "WHERE " << where_clip << "c.parent_id IS NULL ";
    if (best)
        sql << "ORDER BY (COALESCE(va.vote_up,0) - COALESCE(va.vote_down,0)) DESC, c.created_at DESC ";
    else
        sql << "ORDER BY c.created_at DESC ";
    sql << "LIMIT " << limit << " OFFSET " << offset;

    if (mysql_query(conn.raw(), sql.str().c_str())) return {};

    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return {};

    std::vector<models::Comment> comments;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_comment(row, res);
        c.replies = find_replies(c.id, viewer_user_id);
        comments.push_back(std::move(c));
    }

    mysql_free_result(res);
    return comments;
}

std::vector<models::Comment> CommentRepository::find_replies(uint64_t parent_id, uint64_t viewer_user_id) {
    database::ScopedConnection conn(pool_);
    std::ostringstream sql;
    sql << "SELECT c.*, u.username AS author_name, u.avatar_url AS author_avatar, u.`role` AS author_role, "
        << "u.twitch_id AS author_twitch_id, u.badge AS user_badge, "
        << "COALESCE(va.vote_up,0) AS cv_up, COALESCE(va.vote_down,0) AS cv_down, myv.vote AS cv_my_vote "
        << "FROM comments c JOIN users u ON c.user_id=u.id " << vote_join_sql(viewer_user_id) << " "
        << "WHERE c.parent_id=" << parent_id << " ORDER BY c.created_at ASC";

    if (mysql_query(conn.raw(), sql.str().c_str())) return {};

    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return {};

    std::vector<models::Comment> replies;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_comment(row, res);
        c.replies = find_replies(c.id, viewer_user_id);
        replies.push_back(std::move(c));
    }

    mysql_free_result(res);
    return replies;
}

uint64_t CommentRepository::create(const models::Comment& comment) {
    database::ScopedConnection conn(pool_);
    std::string parent = comment.parent_id ? std::to_string(*comment.parent_id) : "NULL";
    std::string b      = database::escape(conn.raw(), comment.body);
    std::string sql    = "INSERT INTO comments (clip_id, user_id, parent_id, body) VALUES ("
        + std::to_string(comment.clip_id) + "," + std::to_string(comment.user_id) + "," + parent + ",'" + b + "')";

    if (mysql_query(conn.raw(), sql.c_str()))
        throw std::runtime_error(std::string("Create comment failed: ") + mysql_error(conn.raw()));

    return mysql_insert_id(conn.raw());
}

bool CommentRepository::update(const models::Comment& comment) {
    database::ScopedConnection conn(pool_);
    std::string b   = database::escape(conn.raw(), comment.body);
    std::string sql = "UPDATE comments SET body='" + b + "' WHERE id=" + std::to_string(comment.id)
        + " AND user_id=" + std::to_string(comment.user_id);
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

bool CommentRepository::remove(uint64_t id) {
    database::ScopedConnection conn(pool_);
    return mysql_query(conn.raw(), ("DELETE FROM comments WHERE id=" + std::to_string(id)).c_str()) == 0;
}

bool CommentRepository::set_comment_vote(uint64_t user_id, uint64_t comment_id, int vote) {
    if (user_id == 0) return false;
    database::ScopedConnection conn(pool_);
    if (vote == 0) {
        std::string sql = "DELETE FROM comment_votes WHERE user_id=" + std::to_string(user_id)
            + " AND comment_id=" + std::to_string(comment_id);
        return mysql_query(conn.raw(), sql.c_str()) == 0;
    }
    if (vote != 1 && vote != -1) return false;
    std::string sql = "INSERT INTO comment_votes (user_id, comment_id, vote) VALUES ("
        + std::to_string(user_id) + "," + std::to_string(comment_id) + "," + std::to_string(vote)
        + ") ON DUPLICATE KEY UPDATE vote=VALUES(vote)";
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

} // namespace clipstream::repositories
