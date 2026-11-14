#include "repositories/CommentRepository.hpp"
#include "repositories/RecommendationWrites.hpp"
#include "database/SqlEscape.hpp"
#include "utils/Errors.hpp"
#include "utils/Utf8.hpp"
#include <algorithm>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace clipstream::repositories {
namespace {

uint64_t clip_owner(MYSQL *c, uint64_t clip_id, bool visible = false) {
    auto sql = "SELECT author_id,clip_creator_twitch_id FROM clips WHERE id=" + std::to_string(clip_id) +
               (visible ? " AND unavailable_at IS NULL AND moderation_hidden=0" : "") + " FOR UPDATE";
    database::query(c, sql.c_str());
    database::Result result(mysql_store_result(c));
    auto row = mysql_fetch_row(result);
    if (!row)
        throw utils::NotFound("Clip not found");
    uint64_t owner = row[0] ? std::stoull(row[0]) : 0;
    std::string creator = row[1] ? row[1] : "";
    result.reset();
    if (!owner && !creator.empty()) {
        auto lookup = "SELECT id FROM users WHERE twitch_id='" + database::escape(c, creator) + "'";
        database::query(c, lookup.c_str());
        database::Result user(mysql_store_result(c));
        auto identity = mysql_fetch_row(user);
        if (identity && identity[0])
            owner = std::stoull(identity[0]);
    }
    return owner;
}

void reputation(MYSQL *c, uint64_t owner, uint64_t author, int delta) {
    if (owner && owner != author) {
        auto sql = "UPDATE users SET reputation=GREATEST(0,reputation+(" + std::to_string(delta) +
                   ")) WHERE id=" + std::to_string(owner);
        database::query(c, sql.c_str());
    }
}
std::string vote_join_sql(uint64_t viewer_id) {
    return "LEFT JOIN comment_votes myv ON myv.comment_id=c.id AND myv.user_id=" + std::to_string(viewer_id);
}

} // namespace

CommentRepository::CommentRepository(database::ConnectionPool &pool) : pool_(pool) {}

models::Comment CommentRepository::row_to_comment(MYSQL_ROW row, MYSQL_RES *res) {
    auto idx = [&](const char *name) -> int {
        unsigned int num = mysql_num_fields(res);
        MYSQL_FIELD *fields = mysql_fetch_fields(res);
        for (unsigned int i = 0; i < num; ++i)
            if (std::strcmp(fields[i].name, name) == 0)
                return static_cast<int>(i);
        return -1;
    };

    models::Comment c;
    int cr = idx("child_count");
    if (cr >= 0 && row[cr])
        c.total_replies = std::stoul(row[cr]);
    c.id = std::stoull(row[idx("id")]);
    c.clip_id = std::stoull(row[idx("clip_id")]);
    c.user_id = std::stoull(row[idx("user_id")]);
    c.body = row[idx("body")] ? row[idx("body")] : "";
    c.created_at = row[idx("created_at")] ? row[idx("created_at")] : "";
    c.updated_at = row[idx("updated_at")] ? row[idx("updated_at")] : "";

    int pi = idx("parent_id");
    if (pi >= 0 && row[pi])
        c.parent_id = std::stoull(row[pi]);

    int ai = idx("author_name");
    if (ai >= 0 && row[ai])
        c.author_name = row[ai];

    int avi = idx("author_avatar");
    if (avi >= 0 && row[avi])
        c.author_avatar = row[avi];

    int ar = idx("author_role");
    if (ar >= 0 && row[ar])
        c.author_role = row[ar];
    else
        c.author_role = "user";

    int tw = idx("author_twitch_id");
    if (tw >= 0 && row[tw])
        c.author_twitch_id = row[tw];

    int ub = idx("user_badge");
    if (ub >= 0 && row[ub] && row[ub][0])
        c.author_cosmetic_badge = row[ub];
    else
        c.author_cosmetic_badge = "none";

    int vu = idx("cv_up");
    if (vu >= 0 && row[vu])
        c.upvotes = static_cast<uint32_t>(std::stoul(row[vu]));
    int vd = idx("cv_down");
    if (vd >= 0 && row[vd])
        c.downvotes = static_cast<uint32_t>(std::stoul(row[vd]));
    int mv = idx("cv_my_vote");
    if (mv >= 0 && row[mv] && row[mv][0])
        c.user_vote = static_cast<int8_t>(std::stoi(row[mv]));

    return c;
}

std::optional<models::Comment> CommentRepository::find_by_id(uint64_t id) {
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT c.*, u.username AS author_name, u.avatar_url AS author_avatar, u.`role` AS author_role, "
        "u.twitch_id AS author_twitch_id, u.badge AS user_badge, "
        "(SELECT COUNT(*) FROM comment_votes vu WHERE vu.comment_id=c.id AND vu.vote=1) AS cv_up, (SELECT "
        "COUNT(*) FROM comment_votes vd WHERE vd.comment_id=c.id AND vd.vote=-1) AS cv_down, NULL AS "
        "cv_my_vote "
        "FROM comments c JOIN users u ON c.user_id=u.id "
        "WHERE c.id=" +
        std::to_string(id);
    if (database::query(conn.raw(), sql.c_str()))
        return std::nullopt;

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        res.reset();
        return std::nullopt;
    }

    auto comment = row_to_comment(row, res);
    res.reset();
    return comment;
}

std::vector<models::Comment> CommentRepository::find_all(uint32_t limit, uint32_t offset) {
    return find_by_clip(0, limit, offset, "recent", 0);
}

std::vector<models::Comment> CommentRepository::find_by_clip(uint64_t clip_id, uint32_t limit,
                                                             uint32_t offset, const std::string &sort,
                                                             uint64_t viewer_user_id, uint64_t parent_id) {
    if (limit < 1)
        limit = 1;
    if (limit > 50)
        limit = 50;
    database::ScopedConnection conn(pool_);
    std::ostringstream sql;
    std::string ordering =
        (sort == "best"
             ? "(SELECT COALESCE(SUM(vote),0) FROM comment_votes vs WHERE vs.comment_id=c.id) DESC,"
             : "");
    ordering += "c.created_at DESC,c.id DESC";
    sql << "WITH RECURSIVE roots AS (SELECT c.id,ROW_NUMBER() OVER(ORDER BY " << ordering
        << ") root_order FROM comments c WHERE c.clip_id=" << clip_id << " AND ";
    if (parent_id)
        sql << "c.parent_id=" << parent_id;
    else
        sql << "c.parent_id IS NULL";
    sql << " ORDER BY " << ordering << " LIMIT " << limit << " OFFSET " << std::min(offset, 10000u)
        << "), tree AS (SELECT c.id,0 depth,r.root_order FROM comments c JOIN roots r ON r.id=c.id UNION ALL "
           "SELECT c.id,t.depth+1,t.root_order FROM comments c JOIN tree t ON c.parent_id=t.id WHERE "
           "c.clip_id="
        << clip_id << " AND t.depth<8 LIMIT 500) ";
    sql << "SELECT c.*,u.username author_name,u.avatar_url author_avatar,u.`role` author_role,u.twitch_id "
           "author_twitch_id,u.badge user_badge, (SELECT COUNT(*) FROM comment_votes vu WHERE "
           "vu.comment_id=c.id AND vu.vote=1) cv_up,(SELECT COUNT(*) FROM comment_votes vd WHERE "
           "vd.comment_id=c.id AND vd.vote=-1) "
           "cv_down,myv.vote cv_my_vote,(SELECT COUNT(*) FROM comments cc WHERE cc.parent_id=c.id) "
           "child_count FROM tree JOIN comments c ON "
           "c.id=tree.id JOIN users u ON u.id=c.user_id "
        << vote_join_sql(viewer_user_id)
        << " ORDER BY tree.depth,tree.root_order,c.created_at,c.id LIMIT 500";
    database::query(conn.raw(), sql.str().c_str());
    database::Result res(mysql_store_result(conn.raw()));
    std::vector<models::Comment> flat;
    while (auto row = mysql_fetch_row(res))
        flat.push_back(row_to_comment(row, res));
    res.reset();
    conn.release();
    std::unordered_map<uint64_t, std::vector<size_t>> children;
    for (size_t i = 0; i < flat.size(); ++i)
        if (flat[i].parent_id)
            children[*flat[i].parent_id].push_back(i);
    std::function<models::Comment(size_t, unsigned)> build = [&](size_t index, unsigned depth) {
        auto c = std::move(flat[index]);
        if (depth < 8)
            for (auto child : children[c.id])
                c.replies.push_back(build(child, depth + 1));
        c.replies_truncated = c.total_replies > c.replies.size();
        return c;
    };
    std::vector<models::Comment> comments;
    for (size_t i = 0; i < flat.size(); ++i)
        if ((!parent_id && !flat[i].parent_id) || (parent_id && flat[i].parent_id == parent_id))
            comments.push_back(build(i, 0));
    return comments;
}

std::vector<models::Comment> CommentRepository::find_replies(uint64_t parent_id, uint64_t viewer_user_id) {
    auto parent = find_by_id(parent_id);
    if (!parent)
        return {};
    return find_by_clip(parent->clip_id, 50, 0, "recent", viewer_user_id, parent_id);
}

uint64_t CommentRepository::create(const models::Comment &comment) {
    return database::retry_transaction([&]() -> uint64_t {
        database::ScopedConnection conn(pool_);
        database::Transaction tx(conn.raw());
        const auto owner = clip_owner(conn.raw(), comment.clip_id, true);
        if (comment.body.empty() || comment.body.size() > 4000)
            throw std::invalid_argument("Comment must be 1-4000 bytes");
        if (comment.parent_id) {
            std::string q =
                "WITH RECURSIVE ancestry AS (SELECT id,parent_id,clip_id,0 depth FROM comments WHERE id=" +
                std::to_string(*comment.parent_id) +
                " UNION ALL SELECT c.id,c.parent_id,c.clip_id,a.depth+1 FROM comments c JOIN ancestry a ON "
                "c.id=a.parent_id WHERE a.depth<8) SELECT COUNT(*),MIN(clip_id),MAX(depth),MAX(clip_id) FROM "
                "ancestry";
            database::query(conn.raw(), q.c_str());
            database::Result r(mysql_store_result(conn.raw()));
            auto row = mysql_fetch_row(r);
            if (!row || !row[0] || std::stoul(row[0]) == 0 || std::stoull(row[1]) != comment.clip_id ||
                std::stoul(row[2]) >= 8 || std::stoull(row[3]) != comment.clip_id)
                throw std::invalid_argument("Invalid comment parent or maximum reply depth exceeded");
        }
        auto lock_users = "SELECT id FROM users WHERE id IN(" + std::to_string(comment.user_id) + "," +
                          std::to_string(owner) + ") ORDER BY id FOR UPDATE";
        database::query(conn.raw(), lock_users.c_str());
        database::Result user_locks(mysql_store_result(conn.raw()));
        user_locks.reset();
        auto allowed = "SELECT id FROM users WHERE id=" + std::to_string(comment.user_id) +
                       " AND account_blocked=0 AND(account_suspended_until IS NULL OR "
                       "account_suspended_until<=UTC_TIMESTAMP()) AND chat_banned_permanently=0 "
                       "AND(chat_banned_until IS NULL OR "
                       "chat_banned_until<=UTC_TIMESTAMP()) FOR UPDATE";
        database::query(conn.raw(), allowed.c_str());
        database::Result permission(mysql_store_result(conn.raw()));
        if (!mysql_fetch_row(permission))
            throw utils::PermissionDenied("Commenting is restricted");
        permission.reset();
        std::string parent = comment.parent_id ? std::to_string(*comment.parent_id) : "NULL";
        std::string b = database::escape(conn.raw(), comment.body);
        std::string sql = "INSERT INTO comments (clip_id, user_id, parent_id, body) VALUES (" +
                          std::to_string(comment.clip_id) + "," + std::to_string(comment.user_id) + "," +
                          parent + ",'" + b + "')";

        if (database::query(conn.raw(), sql.c_str()))
            throw std::runtime_error(std::string("Create comment failed: ") + mysql_error(conn.raw()));

        const auto id = mysql_insert_id(conn.raw());
        mark_recommendation_dirty(conn.raw(),comment.user_id,comment.clip_id);
        if (!comment.parent_id)
            reputation(conn.raw(), owner, comment.user_id, 3);
        else {
            auto q = "SELECT user_id FROM comments WHERE id=" + std::to_string(*comment.parent_id) +
                     " AND clip_id=" + std::to_string(comment.clip_id);
            database::query(conn.raw(), q.c_str());
            database::Result parent_author(mysql_store_result(conn.raw()));
            auto row = mysql_fetch_row(parent_author);
            uint64_t recipient = row && row[0] ? std::stoull(row[0]) : 0;
            parent_author.reset();
            if (recipient && recipient != comment.user_id) {
                nlohmann::json payload = {{"clip_id", comment.clip_id},
                                          {"parent_comment_id", *comment.parent_id},
                                          {"reply_comment_id", id},
                                          {"snippet", utils::utf8_prefix(comment.body, 120)}};
                auto insert = "INSERT INTO notifications(user_id,type,actor_id,payload) VALUES(" +
                              std::to_string(recipient) + ",'comment_reply'," +
                              std::to_string(comment.user_id) + ",'" +
                              database::escape(conn.raw(), payload.dump()) + "')";
                database::query(conn.raw(), insert.c_str());
            }
        }
        tx.commit();
        return id;
    });
}

bool CommentRepository::update(const models::Comment &comment) {
    database::ScopedConnection conn(pool_);
    if (comment.body.empty() || comment.body.size() > 4000)
        throw std::invalid_argument("Comment must be 1-4000 bytes");
    std::string b = database::escape(conn.raw(), comment.body);
    std::string sql = "UPDATE comments SET body='" + b + "' WHERE id=" + std::to_string(comment.id) +
                      " AND user_id=" + std::to_string(comment.user_id);
    database::query(conn.raw(), sql.c_str());
    return mysql_affected_rows(conn.raw()) > 0;
}

bool CommentRepository::remove(uint64_t id) { return remove_authorized(id, 0); }
bool CommentRepository::remove_authorized(uint64_t id, uint64_t actor) {
    return database::retry_transaction([&]() -> bool {
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        auto lookup = "SELECT clip_id FROM comments WHERE id=" + std::to_string(id);
        database::query(c, lookup.c_str());
        database::Result existing(mysql_store_result(c));
        auto initial = mysql_fetch_row(existing);
        if (!initial)
            return false;
        auto clip = std::stoull(initial[0]);
        existing.reset();
        auto owner = clip_owner(c, clip);
        auto q = "SELECT user_id,parent_id FROM comments WHERE id=" + std::to_string(id) + " FOR UPDATE";
        database::query(c, q.c_str());
        database::Result result(mysql_store_result(c));
        auto row = mysql_fetch_row(result);
        if (!row)
            return false;
        uint64_t author = std::stoull(row[0]);
        bool root = row[1] == nullptr;
        result.reset();
        if (actor) {
            auto check = "SELECT `role` FROM users WHERE id=" + std::to_string(actor) +
                         " AND account_blocked=0 AND(account_suspended_until IS NULL OR "
                         "account_suspended_until<=UTC_TIMESTAMP())";
            database::query(c, check.c_str());
            database::Result allowed(mysql_store_result(c));
            auto user = mysql_fetch_row(allowed);
            if (!user ||
                (actor != author && std::string(user[0]) != "admin" && std::string(user[0]) != "moderator"))
                throw utils::PermissionDenied("Cannot delete this comment");
        }
        auto remove = "DELETE FROM comments WHERE id=" + std::to_string(id);
        database::query(c, remove.c_str());
        if (mysql_affected_rows(c) == 0)
            return false;
        if (root)
            reputation(c, owner, author, -3);
        mark_recommendation_dirty(c,author,clip);
        tx.commit();
        return true;
    });
}

bool CommentRepository::set_comment_vote(uint64_t user_id, uint64_t comment_id, int vote) {
    if (user_id == 0)
        return false;
    database::ScopedConnection conn(pool_);
    if (vote == 0) {
        std::string sql = "DELETE FROM comment_votes WHERE user_id=" + std::to_string(user_id) +
                          " AND comment_id=" + std::to_string(comment_id);
        return database::query(conn.raw(), sql.c_str()) == 0;
    }
    if (vote != 1 && vote != -1)
        return false;
    std::string sql = "INSERT INTO comment_votes (user_id, comment_id, vote) VALUES (" +
                      std::to_string(user_id) + "," + std::to_string(comment_id) + "," +
                      std::to_string(vote) + ") ON DUPLICATE KEY UPDATE vote=VALUES(vote)";
    return database::query(conn.raw(), sql.c_str()) == 0;
}

} // namespace clipstream::repositories
