#include "repositories/UserRepository.hpp"
#include "database/SqlEscape.hpp"
#include "repositories/ProfileRepository.hpp"
#include "utils/Errors.hpp"
#include "utils/Utf8.hpp"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace clipstream::repositories {

UserRepository::UserRepository(database::ConnectionPool &pool) : pool_(pool) {}

models::User UserRepository::row_to_user(MYSQL_ROW row, MYSQL_RES *res) {
    auto field_idx = [&](const char *name) -> int {
        unsigned int num = mysql_num_fields(res);
        MYSQL_FIELD *fields = mysql_fetch_fields(res);
        for (unsigned int i = 0; i < num; ++i)
            if (std::strcmp(fields[i].name, name) == 0)
                return static_cast<int>(i);
        return -1;
    };

    models::User u;
    u.id = std::stoull(row[field_idx("id")]);
    int av = field_idx("auth_version");
    if (av >= 0 && row[av])
        u.auth_version = std::stoull(row[av]);
    u.username = row[field_idx("username")] ? row[field_idx("username")] : "";
    u.email = row[field_idx("email")] ? row[field_idx("email")] : "";
    u.password_hash = row[field_idx("password_hash")] ? row[field_idx("password_hash")] : "";
    u.avatar_url = row[field_idx("avatar_url")] ? row[field_idx("avatar_url")] : "";
    u.twitch_id = row[field_idx("twitch_id")] ? row[field_idx("twitch_id")] : "";
    int oat = field_idx("twitch_access_token");
    if (oat >= 0 && row[oat])
        u.twitch_access_token = row[oat];
    int ort = field_idx("twitch_refresh_token");
    if (ort >= 0 && row[ort])
        u.twitch_refresh_token = row[ort];
    int ova = field_idx("oauth_validated_at");
    if (ova >= 0 && row[ova])
        u.oauth_validated_at = row[ova];
    int oex = field_idx("twitch_token_expires_at");
    if (oex >= 0 && row[oex])
        u.twitch_token_expires_at = row[oex];
    u.reputation = std::stoi(row[field_idx("reputation")]);
    u.created_at = row[field_idx("created_at")] ? row[field_idx("created_at")] : "";

    std::string role_str = row[field_idx("role")] ? row[field_idx("role")] : "user";
    if (role_str == "admin")
        u.role = models::UserRole::Admin;
    else if (role_str == "moderator")
        u.role = models::UserRole::Moderator;
    else if (role_str == "vip")
        u.role = models::UserRole::Vip;
    else
        u.role = models::UserRole::User;

    int ab = field_idx("account_blocked");
    if (ab >= 0 && row[ab])
        u.account_blocked = (std::stoi(row[ab]) != 0);
    int suspended = field_idx("account_suspended_until");
    if (suspended >= 0 && row[suspended])
        u.account_suspended_until = row[suspended];
    int permanent = field_idx("chat_banned_permanently");
    if (permanent >= 0 && row[permanent])
        u.chat_banned_permanently = std::string(row[permanent]) == "1";
    int cb = field_idx("chat_banned_until");
    if (cb >= 0 && row[cb])
        u.chat_banned_until = std::string(row[cb]);

    int lcv = field_idx("legal_consent_version");
    if (lcv >= 0 && row[lcv])
        u.legal_consent_version = row[lcv];
    int lca = field_idx("legal_consent_at");
    if (lca >= 0 && row[lca])
        u.legal_consent_at = row[lca];

    std::string badge_str = row[field_idx("badge")] ? row[field_idx("badge")] : "none";
    if (badge_str == "legend")
        u.badge = models::Badge::Legend;
    else if (badge_str == "pro")
        u.badge = models::Badge::Pro;
    else if (badge_str == "rising_star")
        u.badge = models::Badge::RisingStar;
    else
        u.badge = models::Badge::None;

    return u;
}

std::optional<models::User> UserRepository::find_by_id(uint64_t id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT * FROM users WHERE id = " + std::to_string(id);
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

    auto user = row_to_user(row, res);
    res.reset();
    return user;
}

std::vector<models::User> UserRepository::find_all(uint32_t limit, uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT * FROM users ORDER BY id LIMIT " + std::to_string(limit) + " OFFSET " +
                      std::to_string(offset);
    if (database::query(conn.raw(), sql.c_str()))
        return {};

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};

    std::vector<models::User> users;
    while (MYSQL_ROW row = mysql_fetch_row(res))
        users.push_back(row_to_user(row, res));

    res.reset();
    return users;
}

uint64_t UserRepository::create(const models::User &user) {
    database::ScopedConnection conn(pool_);

    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };

    std::string sql = "INSERT INTO users (username, email, password_hash, avatar_url, twitch_id) VALUES ('" +
                      esc(user.username) + "', '" + esc(user.email) + "', '" + esc(user.password_hash) +
                      "', '" + esc(user.avatar_url) + "', " +
                      (user.twitch_id.empty() ? "NULL" : "'" + esc(user.twitch_id) + "'") + ")";

    if (database::query(conn.raw(), sql.c_str()))
        throw std::runtime_error(std::string("Create user failed: ") + mysql_error(conn.raw()));

    return mysql_insert_id(conn.raw());
}

bool UserRepository::update(const models::User &user) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "UPDATE users SET username='" + esc(user.username) + "', avatar_url='" +
                      esc(user.avatar_url) + "' WHERE id=" + std::to_string(user.id);
    return database::query(conn.raw(), sql.c_str()) == 0;
}

bool UserRepository::remove(uint64_t id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "DELETE FROM users WHERE id=" + std::to_string(id);
    return database::query(conn.raw(), sql.c_str()) == 0;
}

std::optional<models::User> UserRepository::find_by_email(const std::string &email) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "SELECT * FROM users WHERE email='" + esc(email) + "'";
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

    auto user = row_to_user(row, res);
    res.reset();
    return user;
}

std::optional<models::User> UserRepository::find_by_username(const std::string &username) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "SELECT * FROM users WHERE username='" + esc(username) + "'";
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

    auto user = row_to_user(row, res);
    res.reset();
    return user;
}

std::optional<models::User> UserRepository::find_by_twitch_id(const std::string &twitch_id) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "SELECT * FROM users WHERE twitch_id='" + esc(twitch_id) + "'";
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

    auto user = row_to_user(row, res);
    res.reset();
    return user;
}

uint64_t UserRepository::clip_owner_user_id_for_reputation(const models::Clip &clip) {
    if (clip.author_id != 0)
        return clip.author_id;
    if (!clip.creator_twitch_id.empty()) {
        if (auto u = find_by_twitch_id(clip.creator_twitch_id))
            return u->id;
    }
    return 0;
}

nlohmann::json UserRepository::get_profile_stats(uint64_t user_id) {
    database::ScopedConnection conn(pool_);
    nlohmann::json stats = {
        {"liked_count", 0},
        {"disliked_count", 0},
        {"unique_viewed_clips", 0},
        {"total_views_events", 0},
        {"comments_count", 0},
        {"friends_count", 0},
        {"following_count", 0},
        {"followers_count", 0},
        {"clips_created_count", 0},
        {"favorite_game_id", 0},
        {"favorite_game_name", ""},
        {"favorite_broadcaster_id", ""},
        {"favorite_broadcaster_name", ""},
        {"night_views", 0},
        {"last_view_at", ""},
        {"wall_posts_count", 0},
    };

    auto one_int = [&](const std::string &sql) -> int {
        if (database::query(conn.raw(), sql.c_str()))
            return 0;
        database::Result res(mysql_store_result(conn.raw()));
        if (!res)
            return 0;
        MYSQL_ROW row = mysql_fetch_row(res);
        int out = (row && row[0]) ? std::stoi(row[0]) : 0;
        res.reset();
        return out;
    };

    stats["liked_count"] =
        one_int("SELECT COUNT(*) FROM likes WHERE user_id=" + std::to_string(user_id) + " AND is_like=1");
    stats["disliked_count"] =
        one_int("SELECT COUNT(*) FROM likes WHERE user_id=" + std::to_string(user_id) + " AND is_like=0");
    stats["unique_viewed_clips"] =
        one_int("SELECT COUNT(DISTINCT clip_id) FROM view_history WHERE user_id=" + std::to_string(user_id));
    stats["total_views_events"] =
        one_int("SELECT COUNT(*) FROM view_history WHERE user_id=" + std::to_string(user_id));
    stats["comments_count"] =
        one_int("SELECT COUNT(*) FROM comments WHERE user_id=" + std::to_string(user_id));
    stats["friends_count"] =
        one_int("SELECT COUNT(*) FROM friendships WHERE user_id=" + std::to_string(user_id));
    stats["following_count"] =
        one_int("SELECT COUNT(*) FROM profile_follows WHERE follower_user_id=" + std::to_string(user_id));
    stats["followers_count"] = one_int("SELECT COUNT(*) FROM profile_follows pf JOIN profile_entities pe ON "
                                       "pe.profile_key=pf.profile_key WHERE pe.local_user_id=" +
                                       std::to_string(user_id));
    stats["clips_created_count"] = one_int(
        "SELECT COUNT(*) FROM clips WHERE unavailable_at IS NULL AND moderation_hidden=0 AND (author_id=" +
        std::to_string(user_id) +
        " OR (clip_creator_twitch_id<>'' AND clip_creator_twitch_id=(SELECT twitch_id FROM users WHERE id=" +
        std::to_string(user_id) + " LIMIT 1)))");
    stats["night_views"] =
        one_int("SELECT COUNT(*) FROM view_history WHERE user_id=" + std::to_string(user_id) +
                " AND (HOUR(viewed_at) BETWEEN 0 AND 5)");
    stats["wall_posts_count"] =
        one_int("SELECT COUNT(*) FROM profile_wall_posts WHERE profile_owner_id=" + std::to_string(user_id));

    {
        std::string sql =
            "SELECT vh.viewed_at FROM view_history vh WHERE vh.user_id=" + std::to_string(user_id) +
            " ORDER BY vh.viewed_at DESC LIMIT 1";
        if (database::query(conn.raw(), sql.c_str()) == 0) {
            database::Result res(mysql_store_result(conn.raw()));
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0])
                    stats["last_view_at"] = std::string(row[0]);
                res.reset();
            }
        }
    }

    {
        std::string sql = "SELECT c.game_id, MAX(g.name) AS game_name, COUNT(*) AS cnt "
                          "FROM view_history vh "
                          "JOIN clips c ON c.id=vh.clip_id "
                          "LEFT JOIN games g ON g.id=c.game_id "
                          "WHERE vh.user_id=" +
                          std::to_string(user_id) +
                          " AND c.game_id IS NOT NULL "
                          "GROUP BY c.game_id "
                          "ORDER BY cnt DESC LIMIT 1";
        if (database::query(conn.raw(), sql.c_str()) == 0) {
            database::Result res(mysql_store_result(conn.raw()));
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0]) {
                    stats["favorite_game_id"] = std::stoull(row[0]);
                    stats["favorite_game_name"] = row[1] ? std::string(row[1]) : "";
                }
                res.reset();
            }
        }
    }

    {
        std::string sql =
            "SELECT c.broadcaster_id, MAX(c.broadcaster_name) AS broadcaster_name, COUNT(*) AS cnt "
            "FROM view_history vh "
            "JOIN clips c ON c.id=vh.clip_id "
            "WHERE vh.user_id=" +
            std::to_string(user_id) +
            " AND c.broadcaster_id<>'' "
            "GROUP BY c.broadcaster_id "
            "ORDER BY cnt DESC LIMIT 1";
        if (database::query(conn.raw(), sql.c_str()) == 0) {
            database::Result res(mysql_store_result(conn.raw()));
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0]) {
                    stats["favorite_broadcaster_id"] = std::string(row[0]);
                    stats["favorite_broadcaster_name"] = row[1] ? std::string(row[1]) : "";
                }
                res.reset();
            }
        }
    }

    return stats;
}

bool UserRepository::update_badge(uint64_t user_id, models::Badge badge) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    auto badge_str = [&](models::Badge b) -> std::string {
        switch (b) {
        case models::Badge::RisingStar:
            return "rising_star";
        case models::Badge::Pro:
            return "pro";
        case models::Badge::Legend:
            return "legend";
        case models::Badge::None:
        default:
            return "none";
        }
    };
    std::string sel = "SELECT badge FROM users WHERE id=" + std::to_string(user_id) + " LIMIT 1";
    if (database::query(conn.raw(), sel.c_str()) != 0)
        return false;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return false;
    MYSQL_ROW row = mysql_fetch_row(res);
    std::string old_badge = (row && row[0]) ? std::string(row[0]) : "none";
    res.reset();

    const std::string new_badge = badge_str(badge);
    if (old_badge == new_badge)
        return true;

    if (old_badge != "none") {
        std::string h = "INSERT INTO user_badge_history (user_id, badge, event_type) VALUES (" +
                        std::to_string(user_id) + ",'" + esc(old_badge) + "','revoked')";
        database::query(conn.raw(), h.c_str());
    }
    if (new_badge != "none") {
        std::string h = "INSERT INTO user_badge_history (user_id, badge, event_type) VALUES (" +
                        std::to_string(user_id) + ",'" + esc(new_badge) + "','granted')";
        database::query(conn.raw(), h.c_str());
    }
    std::string sql = "UPDATE users SET badge='" + esc(new_badge) + "' WHERE id=" + std::to_string(user_id);
    return database::query(conn.raw(), sql.c_str()) == 0;
}

bool UserRepository::update_twitch_oauth(uint64_t id, const std::string &access_token,
                                         const std::string &refresh_token,
                                         const std::string &expires_at_iso) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "UPDATE users SET twitch_access_token='" + esc(access_token) +
                      "', twitch_refresh_token='" + esc(refresh_token) + "', twitch_token_expires_at='" +
                      esc(expires_at_iso) + "' WHERE id=" + std::to_string(id);
    return database::query(conn.raw(), sql.c_str()) == 0;
}

bool UserRepository::clear_twitch_oauth_by_twitch_id(const std::string &twitch_id) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "UPDATE users SET auth_version=auth_version+1, twitch_access_token=NULL, "
                      "twitch_refresh_token=NULL, twitch_token_expires_at=NULL "
                      "WHERE twitch_id='" +
                      esc(twitch_id) + "'";
    return database::query(conn.raw(), sql.c_str()) == 0;
}

bool UserRepository::set_legal_consent(uint64_t user_id, const std::string &version,
                                       const std::string &accepted_at_iso) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "UPDATE users SET legal_consent_version='" + esc(version) + "', legal_consent_at='" +
                      esc(accepted_at_iso) + "' WHERE id=" + std::to_string(user_id);
    return database::query(conn.raw(), sql.c_str()) == 0;
}

namespace {

std::string role_to_db(models::UserRole r) {
    switch (r) {
    case models::UserRole::Admin:
        return "admin";
    case models::UserRole::Moderator:
        return "moderator";
    case models::UserRole::Vip:
        return "vip";
    default:
        return "user";
    }
}

} // namespace

uint32_t UserRepository::count_by_role(models::UserRole role) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT COUNT(*) FROM users WHERE `role`='" + role_to_db(role) + "'";
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

bool UserRepository::update_role(uint64_t user_id, models::UserRole role, uint64_t actor_id) {
    return database::retry_transaction([&]() -> bool {
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        database::query(c, "SELECT id FROM users WHERE `role`='admin' ORDER BY id FOR UPDATE");
        database::Result admins(mysql_store_result(c));
        admins.reset();
        if (actor_id) {
            auto q = "SELECT 1 FROM users WHERE id=" + std::to_string(actor_id) +
                     " AND `role`='admin' AND account_blocked=0 AND (account_suspended_until IS NULL OR "
                     "account_suspended_until<=UTC_TIMESTAMP())";
            database::query(c, q.c_str());
            database::Result actor(mysql_store_result(c));
            if (!mysql_fetch_row(actor))
                throw utils::PermissionDenied("Active administrator required");
        }
        auto q = "SELECT `role` FROM users WHERE id=" + std::to_string(user_id) + " FOR UPDATE";
        database::query(c, q.c_str());
        database::Result target(mysql_store_result(c));
        auto row = mysql_fetch_row(target);
        if (!row)
            throw utils::NotFound("User not found");
        bool was_admin = std::string(row[0]) == "admin";
        target.reset();
        if (was_admin && role != models::UserRole::Admin) {
            database::query(c,
                            "SELECT COUNT(*) FROM users WHERE `role`='admin' AND account_blocked=0 AND "
                            "(account_suspended_until IS NULL OR account_suspended_until<=UTC_TIMESTAMP())");
            database::Result count(mysql_store_result(c));
            auto r = mysql_fetch_row(count);
            if (!r || std::stoul(r[0]) <= 1)
                throw std::invalid_argument("Cannot demote the last active administrator");
        }
        auto update = "UPDATE users SET `role`='" + role_to_db(role) +
                      "',auth_version=auth_version+1 WHERE id=" + std::to_string(user_id);
        database::query(c, update.c_str());
        if (actor_id) {
            auto audit =
                "INSERT INTO moderation_audit(actor_id,target_type,target_id,action,reason,details) VALUES(" +
                std::to_string(actor_id) + ",'user','" + std::to_string(user_id) +
                "','set_role','',JSON_OBJECT('role','" + role_to_db(role) + "'))";
            database::query(c, audit.c_str());
        }
        tx.commit();
        return true;
    });
}

nlohmann::json UserRepository::get_admin_overview_stats() {
    database::ScopedConnection conn(pool_);
    const char *sql = "SELECT "
                      "(SELECT COUNT(*) FROM users) AS users_total, "
                      "(SELECT COUNT(*) FROM users WHERE `role`='admin') AS users_admin, "
                      "(SELECT COUNT(*) FROM users WHERE `role`='moderator') AS users_moderator, "
                      "(SELECT COUNT(*) FROM users WHERE `role`='vip') AS users_vip, "
                      "(SELECT COUNT(*) FROM users WHERE `role`='user') AS users_regular, "
                      "(SELECT COUNT(*) FROM clips) AS clips_total, "
                      "(SELECT COUNT(*) FROM comments) AS comments_total, "
                      "(SELECT COUNT(*) FROM view_history WHERE viewed_at >= DATE_SUB(UTC_TIMESTAMP(), "
                      "INTERVAL 24 HOUR)) AS "
                      "views_24h";
    nlohmann::json out = nlohmann::json::object();
    if (database::query(conn.raw(), sql) != 0)
        return out;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return out;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        auto num = [&](int i) -> uint64_t { return (row[i] && row[i][0]) ? std::stoull(row[i]) : 0; };
        out["users_total"] = num(0);
        out["users_admin"] = num(1);
        out["users_moderator"] = num(2);
        out["users_vip"] = num(3);
        out["users_regular"] = num(4);
        out["clips_total"] = num(5);
        out["comments_total"] = num(6);
        out["views_24h"] = num(7);
    }
    res.reset();
    return out;
}

std::vector<nlohmann::json> UserRepository::list_users_admin(uint32_t limit, uint32_t offset,
                                                             const std::string &search) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };

    std::string where = "1=1";
    if (!search.empty()) {
        std::string q = esc(search);
        where += " AND (username LIKE '%" + q + "%' OR email LIKE '%" + q + "%')";
    }
    std::string sql =
        "SELECT id, username, email, avatar_url, `role`, reputation, created_at FROM users WHERE " + where +
        " ORDER BY id DESC LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);

    std::vector<nlohmann::json> out;
    if (database::query(conn.raw(), sql.c_str()))
        return out;

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return out;

    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        nlohmann::json u = {
            {"id", std::stoull(row[0])},
            {"username", row[1] ? std::string(row[1]) : ""},
            {"email", row[2] ? std::string(row[2]) : ""},
            {"avatar_url", row[3] ? std::string(row[3]) : ""},
            {"role", row[4] ? std::string(row[4]) : "user"},
            {"reputation", row[5] ? std::stoi(row[5]) : 0},
            {"created_at", row[6] ? std::string(row[6]) : ""},
        };
        out.push_back(std::move(u));
    }
    res.reset();
    return out;
}

uint64_t UserRepository::count_users_admin(const std::string &search) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };

    std::string where = "1=1";
    if (!search.empty()) {
        std::string q = esc(search);
        where += " AND (username LIKE '%" + q + "%' OR email LIKE '%" + q + "%')";
    }
    std::string sql = "SELECT COUNT(*) FROM users WHERE " + where;
    if (database::query(conn.raw(), sql.c_str()))
        return 0;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    uint64_t n = 0;
    if (row && row[0])
        n = std::stoull(row[0]);
    res.reset();
    return n;
}

bool UserRepository::may_post_comment(uint64_t user_id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT COUNT(*) FROM users WHERE id=" + std::to_string(user_id) +
                      " AND account_blocked=0 AND (account_suspended_until IS NULL OR "
                      "account_suspended_until<=UTC_TIMESTAMP()) AND chat_banned_permanently=0 AND "
                      "(chat_banned_until IS NULL OR "
                      "chat_banned_until<=UTC_TIMESTAMP())";
    if (database::query(conn.raw(), sql.c_str()))
        return false;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return false;
    MYSQL_ROW row = mysql_fetch_row(res);
    bool ok = row && row[0] && std::stoul(row[0]) > 0;
    res.reset();
    return ok;
}

bool UserRepository::follow(uint64_t follower, uint64_t target) { return try_follow(follower, target) >= 0; }
int UserRepository::try_follow(uint64_t follower, uint64_t target) {
    if (!follower || follower == target)
        return 0;
    auto user = find_by_id(target);
    if (!user || user->account_blocked)
        return -1;
    ProfileRepository profiles(pool_);
    auto entity = profiles.ensure_user(*user);
    return profiles.follow(follower, entity.key) ? 1 : 0;
}

std::unordered_set<uint64_t> UserRepository::following_ids_among(uint64_t follower,
                                                                 const std::vector<uint64_t> &ids) {
    std::unordered_set<uint64_t> out;
    if (!follower || ids.empty())
        return out;
    database::ScopedConnection conn(pool_);
    std::ostringstream values;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i)
            values << ",";
        values << ids[i];
    }
    auto sql = "SELECT pe.local_user_id FROM profile_follows pf JOIN profile_entities pe ON "
               "pe.profile_key=pf.profile_key WHERE pf.follower_user_id=" +
               std::to_string(follower) + " AND pe.local_user_id IN(" + values.str() + ")";
    database::query(conn.raw(), sql.c_str());
    database::Result result(mysql_store_result(conn.raw()));
    while (auto row = mysql_fetch_row(result))
        if (row[0])
            out.insert(std::stoull(row[0]));
    return out;
}
bool UserRepository::unfollow(uint64_t follower, uint64_t target) {
    auto user = find_by_id(target);
    if (!user)
        return false;
    ProfileRepository profiles(pool_);
    auto entity = profiles.ensure_user(*user);
    profiles.unfollow(follower, entity.key);
    return true;
}
bool UserRepository::is_following(uint64_t follower, uint64_t target) {
    auto user = find_by_id(target);
    if (!user)
        return false;
    ProfileRepository profiles(pool_);
    auto entity = profiles.ensure_user(*user);
    return profiles.is_following(follower, entity.key);
}

nlohmann::json UserRepository::get_user_settings(uint64_t user_id) {
    database::ScopedConnection conn(pool_);
    nlohmann::json out = {{"hide_mature", true},
                          {"mute_keywords", nlohmann::json::array()},
                          {"clip_languages", nlohmann::json::array()}};
    std::string sql = "SELECT hide_mature, mute_keywords, clip_languages FROM user_settings WHERE user_id=" +
                      std::to_string(user_id) + " LIMIT 1";
    if (database::query(conn.raw(), sql.c_str()) != 0)
        return out;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return out;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        if (row[0])
            out["hide_mature"] = (std::stoi(row[0]) != 0);
        if (row[1]) {
            try {
                auto j = nlohmann::json::parse(row[1]);
                if (j.is_array())
                    out["mute_keywords"] = std::move(j);
            } catch (...) {
            }
        }
        if (row[2]) {
            try {
                auto j = nlohmann::json::parse(row[2]);
                if (j.is_array())
                    out["clip_languages"] = std::move(j);
            } catch (...) {
            }
        }
    }
    res.reset();
    return out;
}

bool UserRepository::save_user_settings(uint64_t user_id, bool hide_mature,
                                        const std::vector<std::string> &mute_keywords,
                                        const std::vector<std::string> &clip_languages) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &s : mute_keywords) {
        if (s.empty())
            continue;
        std::string t = s.size() > 64 ? utils::utf8_prefix(s, 64) : s;
        arr.push_back(t);
        if (arr.size() >= 32)
            break;
    }
    nlohmann::json lang_arr = nlohmann::json::array();
    for (const auto &s : clip_languages) {
        std::string norm;
        for (char ch : s) {
            if (ch >= 'A' && ch <= 'Z')
                norm += static_cast<char>(ch - 'A' + 'a');
            else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')
                norm += ch;
            if (norm.size() >= 16)
                break;
        }
        if (!norm.empty()) {
            lang_arr.push_back(norm);
            if (lang_arr.size() >= 24)
                break;
        }
    }
    const std::string jdump = esc(arr.dump());
    const std::string jlang = esc(lang_arr.dump());
    std::string sql =
        "INSERT INTO user_settings (user_id, hide_mature, mute_keywords, clip_languages) VALUES (" +
        std::to_string(user_id) + "," + std::string(hide_mature ? "1" : "0") + ",'" + jdump + "','" + jlang +
        "') "
        "ON DUPLICATE KEY UPDATE hide_mature=VALUES(hide_mature), mute_keywords=VALUES(mute_keywords), "
        "clip_languages=VALUES(clip_languages)";
    return database::query(conn.raw(), sql.c_str()) == 0;
}

std::vector<nlohmann::json> UserRepository::get_badge_history(uint64_t user_id, uint32_t limit) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT id, badge, event_type, created_at FROM user_badge_history WHERE user_id=" +
                      std::to_string(user_id) + " ORDER BY created_at DESC LIMIT " +
                      std::to_string(std::max(1u, limit));
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<nlohmann::json> out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"badge", row[1] ? std::string(row[1]) : ""},
            {"event_type", row[2] ? std::string(row[2]) : ""},
            {"created_at", row[3] ? std::string(row[3]) : ""},
        });
    }
    res.reset();
    return out;
}

std::optional<nlohmann::json> UserRepository::get_public_profile(const std::string &username,
                                                                 uint64_t viewer_id) {
    auto u = find_by_username(username);
    if (!u)
        return std::nullopt;
    nlohmann::json j = {
        {"id", u->id},
        {"username", u->username},
        {"avatar_url", u->avatar_url},
        {"reputation", u->reputation},
        {"badge", static_cast<int>(u->badge)},
        {"created_at", u->created_at},
    };
    if (!u->twitch_id.empty())
        j["twitch_id"] = u->twitch_id;
    else
        j["twitch_id"] = nullptr;
    j["stats"] = get_profile_stats(u->id);
    j["badge_history"] = get_badge_history(u->id, 50);
    j["friendship"] = friendship_status(viewer_id, u->id);
    {
        auto ach = list_earned_achievements(u->id, 64);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &row : ach)
            arr.push_back(row);
        j["achievements_earned"] = std::move(arr);
    }
    return j;
}

bool UserRepository::has_recent_site_activity(uint64_t user_id, unsigned window_minutes) {
    if (user_id == 0)
        return false;
    unsigned w = std::max(1u, std::min(120u, window_minutes));
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT 1 FROM analytics_sessions WHERE user_id=" + std::to_string(user_id) +
                      " AND last_seen_at >= UTC_TIMESTAMP() - INTERVAL " + std::to_string(w) +
                      " MINUTE LIMIT 1";
    if (database::query(conn.raw(), sql.c_str()))
        return false;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return false;
    const bool ok = mysql_fetch_row(res) != nullptr;
    res.reset();
    return ok;
}

std::optional<nlohmann::json> UserRepository::find_site_user_by_twitch_id(const std::string &twitch_id) {
    if (twitch_id.empty())
        return std::nullopt;
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "SELECT id, username FROM users WHERE twitch_id='" + esc(twitch_id) +
                      "' AND account_blocked=0 LIMIT 1";
    if (database::query(conn.raw(), sql.c_str()))
        return std::nullopt;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return std::nullopt;
    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<nlohmann::json> out;
    if (row && row[0] && row[1]) {
        out = nlohmann::json{
            {"id", std::stoull(row[0])},
            {"username", std::string(row[1])},
        };
    }
    res.reset();
    return out;
}

std::vector<nlohmann::json> UserRepository::list_followers(uint64_t id, uint32_t limit, uint32_t offset) {
    auto user = find_by_id(id);
    if (!user)
        return {};
    ProfileRepository profiles(pool_);
    auto entity = profiles.ensure_user(*user);
    std::vector<nlohmann::json> out;
    for (const auto &item : profiles.followers(entity.key, limit, offset))
        out.push_back(ProfileRepository::card(item));
    return out;
}
std::vector<nlohmann::json> UserRepository::list_following(uint64_t id, uint32_t limit, uint32_t offset) {
    ProfileRepository profiles(pool_);
    std::vector<nlohmann::json> out;
    for (const auto &item : profiles.following(id, limit, offset))
        out.push_back(ProfileRepository::card(item));
    return out;
}

std::vector<nlohmann::json> UserRepository::search_users_by_username_prefix(const std::string &query,
                                                                            uint32_t limit) {
    if (query.size() < 2)
        return {};
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string q = esc(query);
    std::string sql =
        "SELECT id, username, avatar_url FROM users WHERE account_blocked = 0 AND username LIKE '%" + q +
        "%' ORDER BY username ASC LIMIT " + std::to_string(std::max(1u, std::min(50u, limit)));
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<nlohmann::json> out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"username", row[1] ? std::string(row[1]) : ""},
            {"avatar_url", row[2] ? std::string(row[2]) : ""},
        });
    }
    res.reset();
    return out;
}

std::string UserRepository::friendship_status(uint64_t viewer_id, uint64_t target_id) {
    if (viewer_id == 0)
        return "none";
    if (viewer_id == target_id)
        return "self";
    std::vector<uint64_t> v{target_id};
    auto m = friendship_status_bulk(viewer_id, v);
    auto it = m.find(target_id);
    return it == m.end() ? "none" : it->second;
}

std::unordered_map<uint64_t, std::string>
UserRepository::friendship_status_bulk(uint64_t viewer_id, const std::vector<uint64_t> &target_ids) {
    std::unordered_map<uint64_t, std::string> out;
    if (viewer_id == 0 || target_ids.empty())
        return out;
    std::vector<uint64_t> ids;
    ids.reserve(target_ids.size());
    for (uint64_t t : target_ids) {
        if (t == viewer_id) {
            out[t] = "self";
        } else {
            if (out.find(t) == out.end()) {
                out[t] = "none";
                ids.push_back(t);
            }
        }
    }
    if (ids.empty())
        return out;
    database::ScopedConnection conn(pool_);
    auto in_list = [&](const std::vector<uint64_t> &v) {
        std::ostringstream o;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i)
                o << ",";
            o << v[i];
        }
        return o.str();
    };
    const std::string in = in_list(ids);
    {
        std::string sql = "SELECT friend_id FROM friendships WHERE user_id=" + std::to_string(viewer_id) +
                          " AND friend_id IN (" + in + ")";
        if (database::query(conn.raw(), sql.c_str()) == 0) {
            database::Result res(mysql_store_result(conn.raw()));
            if (res) {
                while (MYSQL_ROW row = mysql_fetch_row(res)) {
                    if (row[0])
                        out[std::stoull(row[0])] = "friends";
                }
                res.reset();
            }
        }
    }
    std::vector<uint64_t> rest;
    for (uint64_t t : ids) {
        if (out[t] != "friends")
            rest.push_back(t);
    }
    if (rest.empty())
        return out;
    const std::string in2 = in_list(rest);
    {
        std::string sql =
            "SELECT addressee_id FROM friend_requests WHERE requester_id=" + std::to_string(viewer_id) +
            " AND addressee_id IN (" + in2 + ")";
        if (database::query(conn.raw(), sql.c_str()) == 0) {
            database::Result res(mysql_store_result(conn.raw()));
            if (res) {
                while (MYSQL_ROW row = mysql_fetch_row(res)) {
                    if (row[0])
                        out[std::stoull(row[0])] = "outgoing";
                }
                res.reset();
            }
        }
    }
    {
        std::string sql =
            "SELECT requester_id FROM friend_requests WHERE addressee_id=" + std::to_string(viewer_id) +
            " AND requester_id IN (" + in2 + ")";
        if (database::query(conn.raw(), sql.c_str()) == 0) {
            database::Result res(mysql_store_result(conn.raw()));
            if (res) {
                while (MYSQL_ROW row = mysql_fetch_row(res)) {
                    if (row[0]) {
                        uint64_t rid = std::stoull(row[0]);
                        if (out[rid] == "none")
                            out[rid] = "incoming";
                    }
                }
                res.reset();
            }
        }
    }
    return out;
}

static bool mysql_exec_ok(MYSQL *raw, const std::string &sql) {
    return database::query(raw, sql.c_str()) == 0;
}

int UserRepository::send_friend_request(uint64_t requester, uint64_t addressee) {
    return database::retry_transaction([&]() -> int {
        if (!requester || !addressee || requester == addressee)
            return -1;
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        auto pair = "SELECT id FROM users WHERE id IN(" + std::to_string(requester) + "," +
                    std::to_string(addressee) +
                    ") AND account_blocked=0 AND (account_suspended_until IS NULL OR "
                    "account_suspended_until<=UTC_TIMESTAMP()) ORDER BY id FOR UPDATE";
        database::query(c, pair.c_str());
        database::Result users(mysql_store_result(c));
        if (mysql_num_rows(users) != 2)
            return -1;
        users.reset();
        auto existing = "SELECT 1 FROM friendships WHERE user_id=" + std::to_string(requester) +
                        " AND friend_id=" + std::to_string(addressee);
        database::query(c, existing.c_str());
        database::Result friends(mysql_store_result(c));
        if (mysql_fetch_row(friends)) {
            tx.commit();
            return 0;
        }
        friends.reset();
        auto reverse = "SELECT 1 FROM friend_requests WHERE requester_id=" + std::to_string(addressee) +
                       " AND addressee_id=" + std::to_string(requester);
        database::query(c, reverse.c_str());
        database::Result pending(mysql_store_result(c));
        bool mutual = mysql_fetch_row(pending) != nullptr;
        pending.reset();
        if (mutual) {
            auto clear = "DELETE FROM friend_requests WHERE (requester_id=" + std::to_string(requester) +
                         " AND addressee_id=" + std::to_string(addressee) +
                         ") OR(requester_id=" + std::to_string(addressee) +
                         " AND addressee_id=" + std::to_string(requester) + ")";
            database::query(c, clear.c_str());
            auto link = "INSERT IGNORE INTO friendships(user_id,friend_id) VALUES(" +
                        std::to_string(requester) + "," + std::to_string(addressee) + "),(" +
                        std::to_string(addressee) + "," + std::to_string(requester) + ")";
            database::query(c, link.c_str());
            tx.commit();
            return 2;
        }
        auto insert = "INSERT IGNORE INTO friend_requests(requester_id,addressee_id) VALUES(" +
                      std::to_string(requester) + "," + std::to_string(addressee) + ")";
        database::query(c, insert.c_str());
        int result = mysql_affected_rows(c) == 1 ? 1 : 0;
        tx.commit();
        return result;
    });
}

bool UserRepository::cancel_friend_request(uint64_t requester_id, uint64_t addressee_id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "DELETE FROM friend_requests WHERE requester_id=" + std::to_string(requester_id) +
                      " AND addressee_id=" + std::to_string(addressee_id);
    return mysql_exec_ok(conn.raw(), sql);
}

bool UserRepository::accept_friend_request(uint64_t addressee, uint64_t requester) {
    return database::retry_transaction([&]() -> bool {
        if (!addressee || !requester || addressee == requester)
            return false;
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        auto pair = "SELECT id FROM users WHERE id IN(" + std::to_string(requester) + "," +
                    std::to_string(addressee) +
                    ") AND account_blocked=0 AND (account_suspended_until IS NULL OR "
                    "account_suspended_until<=UTC_TIMESTAMP()) ORDER BY id FOR UPDATE";
        database::query(c, pair.c_str());
        database::Result users(mysql_store_result(c));
        if (mysql_num_rows(users) != 2)
            return false;
        users.reset();
        auto remove = "DELETE FROM friend_requests WHERE requester_id=" + std::to_string(requester) +
                      " AND addressee_id=" + std::to_string(addressee);
        database::query(c, remove.c_str());
        if (mysql_affected_rows(c) == 0)
            return false;
        auto reverse = "DELETE FROM friend_requests WHERE requester_id=" + std::to_string(addressee) +
                       " AND addressee_id=" + std::to_string(requester);
        database::query(c, reverse.c_str());
        auto insert = "INSERT IGNORE INTO friendships(user_id,friend_id) VALUES(" +
                      std::to_string(requester) + "," + std::to_string(addressee) + "),(" +
                      std::to_string(addressee) + "," + std::to_string(requester) + ")";
        database::query(c, insert.c_str());
        tx.commit();
        return true;
    });
}

bool UserRepository::decline_friend_request(uint64_t addressee_id, uint64_t requester_id) {
    return cancel_friend_request(requester_id, addressee_id);
}

bool UserRepository::remove_friendship(uint64_t user_a, uint64_t user_b) {
    if (user_a == 0 || user_b == 0)
        return false;
    database::ScopedConnection conn(pool_);
    std::string sql = "DELETE FROM friendships WHERE (user_id=" + std::to_string(user_a) +
                      " AND friend_id=" + std::to_string(user_b) + ") OR (user_id=" + std::to_string(user_b) +
                      " AND friend_id=" + std::to_string(user_a) + ")";
    return mysql_exec_ok(conn.raw(), sql);
}

std::vector<nlohmann::json> UserRepository::list_friends(uint64_t user_id, uint32_t limit, uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT u.id, u.username, u.avatar_url, f.created_at AS friends_since "
                      "FROM friendships f INNER JOIN users u ON u.id = f.friend_id "
                      "WHERE f.user_id = " +
                      std::to_string(user_id) +
                      " AND u.account_blocked = 0 ORDER BY f.created_at DESC LIMIT " + std::to_string(limit) +
                      " OFFSET " + std::to_string(offset);
    std::vector<nlohmann::json> out;
    if (database::query(conn.raw(), sql.c_str()))
        return out;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"username", row[1] ? std::string(row[1]) : ""},
            {"avatar_url", row[2] ? std::string(row[2]) : ""},
            {"since", row[3] ? std::string(row[3]) : ""},
        });
    }
    res.reset();
    return out;
}

std::vector<nlohmann::json> UserRepository::list_friend_requests_incoming(uint64_t user_id, uint32_t limit,
                                                                          uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT u.id, u.username, u.avatar_url, r.created_at AS requested_at "
                      "FROM friend_requests r INNER JOIN users u ON u.id = r.requester_id "
                      "WHERE r.addressee_id = " +
                      std::to_string(user_id) +
                      " AND u.account_blocked = 0 ORDER BY r.created_at DESC LIMIT " + std::to_string(limit) +
                      " OFFSET " + std::to_string(offset);
    std::vector<nlohmann::json> out;
    if (database::query(conn.raw(), sql.c_str()))
        return out;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"username", row[1] ? std::string(row[1]) : ""},
            {"avatar_url", row[2] ? std::string(row[2]) : ""},
            {"since", row[3] ? std::string(row[3]) : ""},
        });
    }
    res.reset();
    return out;
}

std::vector<nlohmann::json> UserRepository::list_friend_requests_outgoing(uint64_t user_id, uint32_t limit,
                                                                          uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT u.id, u.username, u.avatar_url, r.created_at AS requested_at "
                      "FROM friend_requests r INNER JOIN users u ON u.id = r.addressee_id "
                      "WHERE r.requester_id = " +
                      std::to_string(user_id) +
                      " AND u.account_blocked = 0 ORDER BY r.created_at DESC LIMIT " + std::to_string(limit) +
                      " OFFSET " + std::to_string(offset);
    std::vector<nlohmann::json> out;
    if (database::query(conn.raw(), sql.c_str()))
        return out;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"username", row[1] ? std::string(row[1]) : ""},
            {"avatar_url", row[2] ? std::string(row[2]) : ""},
            {"since", row[3] ? std::string(row[3]) : ""},
        });
    }
    res.reset();
    return out;
}

bool UserRepository::sync_earned_achievements(uint64_t user_id, const std::vector<std::string> &keys) {
    if (user_id == 0 || keys.empty())
        return true;
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    for (const auto &k : keys) {
        if (k.empty() || k.size() > 64)
            continue;
        std::string sql = "INSERT IGNORE INTO user_achievements (user_id, achievement_key) VALUES (" +
                          std::to_string(user_id) + ",'" + esc(k) + "')";
        database::query(conn.raw(), sql.c_str());
    }
    return true;
}

std::vector<nlohmann::json> UserRepository::list_earned_achievements(uint64_t user_id, uint32_t limit) {
    if (limit < 1)
        limit = 1;
    if (limit > 128)
        limit = 128;
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT achievement_key, earned_at FROM user_achievements WHERE user_id=" + std::to_string(user_id) +
        " ORDER BY earned_at DESC LIMIT " + std::to_string(limit);
    std::vector<nlohmann::json> out;
    if (database::query(conn.raw(), sql.c_str()))
        return out;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"key", row[0] ? std::string(row[0]) : ""},
            {"earned_at", row[1] ? std::string(row[1]) : ""},
        });
    }
    res.reset();
    return out;
}

std::vector<nlohmann::json> UserRepository::list_wall_posts(uint64_t profile_owner_id, uint32_t limit,
                                                            uint32_t offset) {
    if (limit < 1)
        limit = 1;
    if (limit > 50)
        limit = 50;
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT p.id, p.body, p.created_at, u.id AS aid, u.username AS aname, u.avatar_url AS aavatar "
        "FROM profile_wall_posts p INNER JOIN users u ON u.id=p.author_id "
        "WHERE p.profile_owner_id=" +
        std::to_string(profile_owner_id) + " ORDER BY p.created_at DESC LIMIT " + std::to_string(limit) +
        " OFFSET " + std::to_string(offset);
    std::vector<nlohmann::json> out;
    struct Row {
        uint64_t pid{};
        std::string body;
        std::string created_at;
        uint64_t aid{};
        std::string aname;
        std::string aavatar;
    };
    std::vector<Row> rows;
    if (database::query(conn.raw(), sql.c_str()))
        return out;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        Row r;
        r.pid = row[0] ? std::stoull(row[0]) : 0;
        r.body = row[1] ? std::string(row[1]) : "";
        r.created_at = row[2] ? std::string(row[2]) : "";
        r.aid = row[3] ? std::stoull(row[3]) : 0;
        r.aname = row[4] ? std::string(row[4]) : "";
        r.aavatar = row[5] ? std::string(row[5]) : "";
        rows.push_back(std::move(r));
    }
    res.reset();

    std::unordered_map<uint64_t, nlohmann::json> mentions_by_post;
    if (!rows.empty()) {
        std::ostringstream ids;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (i)
                ids << ",";
            ids << rows[i].pid;
        }
        std::string mq =
            "SELECT m.post_id, m.mention_kind, m.mentioned_user_id, m.mentioned_clip_id, u.username AS mun "
            "FROM profile_wall_mentions m LEFT JOIN users u ON u.id=m.mentioned_user_id WHERE m.post_id IN "
            "(" +
            ids.str() + ")";
        if (database::query(conn.raw(), mq.c_str()) == 0) {
            database::Result mr(mysql_store_result(conn.raw()));
            if (mr) {
                while (MYSQL_ROW r2 = mysql_fetch_row(mr)) {
                    uint64_t pid = r2[0] ? std::stoull(r2[0]) : 0;
                    nlohmann::json m;
                    m["kind"] = r2[1] ? std::string(r2[1]) : "";
                    if (r2[2])
                        m["user_id"] = std::stoull(r2[2]);
                    if (r2[3])
                        m["clip_id"] = std::stoull(r2[3]);
                    if (r2[4] && r2[4][0])
                        m["username"] = std::string(r2[4]);
                    if (mentions_by_post.find(pid) == mentions_by_post.end())
                        mentions_by_post[pid] = nlohmann::json::array();
                    mentions_by_post[pid].push_back(std::move(m));
                }
                mr.reset();
            }
        }
    }

    for (const auto &r : rows) {
        nlohmann::json mentions = nlohmann::json::array();
        auto it = mentions_by_post.find(r.pid);
        if (it != mentions_by_post.end())
            mentions = it->second;
        out.push_back(nlohmann::json{
            {"id", r.pid},
            {"body", r.body},
            {"created_at", r.created_at},
            {"author", {{"id", r.aid}, {"username", r.aname}, {"avatar_url", r.aavatar}}},
            {"mentions", std::move(mentions)},
        });
    }
    return out;
}

std::vector<uint64_t> UserRepository::wall_post_mentioned_user_ids(uint64_t post_id) {
    std::vector<uint64_t> out;
    if (post_id == 0)
        return out;
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT mentioned_user_id FROM profile_wall_mentions WHERE post_id=" + std::to_string(post_id) +
        " AND mention_kind='user' AND mentioned_user_id IS NOT NULL";
    if (database::query(conn.raw(), sql.c_str()))
        return out;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        if (row[0])
            out.push_back(std::stoull(row[0]));
    }
    res.reset();
    return out;
}

std::optional<uint64_t> UserRepository::create_wall_post(uint64_t author_id, uint64_t profile_owner_id,
                                                         const std::string &body) {
    return database::retry_transaction([&]() -> std::optional<uint64_t> {
        if (author_id == 0 || profile_owner_id == 0 || !may_post_comment(author_id) ||
            !ProfileRepository(pool_).may_post_wall(profile_owner_id, author_id))
            return std::nullopt;
        if (body.empty() || body.size() > 4000)
            return std::nullopt;
        database::ScopedConnection conn(pool_);
        database::Transaction tx(conn.raw());
        auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
        std::string sql = "INSERT INTO profile_wall_posts (profile_owner_id, author_id, body) VALUES (" +
                          std::to_string(profile_owner_id) + "," + std::to_string(author_id) + ",'" +
                          esc(body) + "')";
        if (!mysql_exec_ok(conn.raw(), sql))
            return std::nullopt;
        uint64_t post_id = static_cast<uint64_t>(mysql_insert_id(conn.raw()));
        if (post_id == 0)
            return std::nullopt;

        auto is_user_char = [](char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                   ch == '_';
        };
        size_t i = 0;
        while (i < body.size()) {
            if (body[i] != '@') {
                ++i;
                continue;
            }
            size_t j = i + 1;
            if (j + 5 <= body.size() && body.compare(j, 5, "clip/") == 0) {
                j += 5;
                size_t k = j;
                while (k < body.size() && body[k] >= '0' && body[k] <= '9')
                    ++k;
                if (k > j) {
                    uint64_t cid = 0;
                    auto parsed = std::from_chars(body.data() + j, body.data() + k, cid);
                    if (parsed.ec != std::errc{} || parsed.ptr != body.data() + k) {
                        i = k;
                        continue;
                    }
                    std::string ck = "SELECT id FROM clips WHERE id=" + std::to_string(cid) + " LIMIT 1";
                    if (database::query(conn.raw(), ck.c_str()) == 0) {
                        database::Result rs(mysql_store_result(conn.raw()));
                        bool ok = rs && mysql_fetch_row(rs);
                        if (rs)
                            rs.reset();
                        if (ok) {
                            std::string ins = "INSERT INTO profile_wall_mentions (post_id, mention_kind, "
                                              "mentioned_user_id, mentioned_clip_id) VALUES (" +
                                              std::to_string(post_id) + ",'clip',NULL," +
                                              std::to_string(cid) + ")";
                            database::query(conn.raw(), ins.c_str());
                        }
                    }
                }
                i = k;
                continue;
            }
            size_t start = j;
            while (j < body.size() && is_user_char(body[j]))
                ++j;
            if (j - start >= 2 && j - start <= 32) {
                std::string uname = body.substr(start, j - start);
                std::string qu =
                    "SELECT id FROM users WHERE account_blocked=0 AND username='" + esc(uname) + "' LIMIT 1";
                if (database::query(conn.raw(), qu.c_str()) == 0) {
                    database::Result rs(mysql_store_result(conn.raw()));
                    MYSQL_ROW rw = rs ? mysql_fetch_row(rs) : nullptr;
                    uint64_t uid = 0;
                    if (rw && rw[0])
                        uid = std::stoull(rw[0]);
                    if (rs)
                        rs.reset();
                    if (uid > 0 && uid != author_id) {
                        std::string ins = "INSERT INTO profile_wall_mentions (post_id, mention_kind, "
                                          "mentioned_user_id, mentioned_clip_id) VALUES (" +
                                          std::to_string(post_id) + ",'user'," + std::to_string(uid) +
                                          ",NULL)";
                        database::query(conn.raw(), ins.c_str());
                    }
                }
            }
            i = j;
        }
        auto recipients_query =
            "SELECT DISTINCT mentioned_user_id FROM profile_wall_mentions WHERE post_id=" +
            std::to_string(post_id) + " AND mentioned_user_id IS NOT NULL AND mentioned_user_id<>" +
            std::to_string(author_id);
        database::query(conn.raw(), recipients_query.c_str());
        database::Result recipients(mysql_store_result(conn.raw()));
        std::vector<uint64_t> ids;
        while (auto row = mysql_fetch_row(recipients))
            ids.push_back(std::stoull(row[0]));
        recipients.reset();
        if (ids.size() > 20)
            throw std::invalid_argument("A wall post can mention at most 20 users");
        auto owner_query = "SELECT username FROM users WHERE id=" + std::to_string(profile_owner_id);
        database::query(conn.raw(), owner_query.c_str());
        database::Result owner(mysql_store_result(conn.raw()));
        auto owner_row = mysql_fetch_row(owner);
        std::string owner_name = owner_row && owner_row[0] ? owner_row[0] : "";
        owner.reset();
        for (auto recipient : ids) {
            nlohmann::json payload = {{"post_id", post_id}, {"profile_username", owner_name}};
            auto notify = "INSERT INTO notifications(user_id,type,actor_id,payload) VALUES(" +
                          std::to_string(recipient) + ",'wall_mention'," + std::to_string(author_id) + ",'" +
                          esc(payload.dump()) + "')";
            database::query(conn.raw(), notify.c_str());
        }
        tx.commit();
        return post_id;
    });
}

std::optional<models::User> UserRepository::find_by_access_token(const std::string &token) {
    database::ScopedConnection conn(pool_);
    auto sql =
        "SELECT * FROM users WHERE twitch_access_token='" + database::escape(conn.raw(), token) + "' LIMIT 1";
    database::query(conn.raw(), sql.c_str());
    database::Result result(mysql_store_result(conn.raw()));
    auto row = mysql_fetch_row(result);
    if (!row)
        return std::nullopt;
    return row_to_user(row, result);
}
std::vector<models::User> UserRepository::oauth_users_for_validation(uint64_t after, uint32_t limit) {
    database::ScopedConnection conn(pool_);
    auto sql = "SELECT * FROM users WHERE id>" + std::to_string(after) +
               " AND twitch_access_token IS NOT NULL AND twitch_access_token<>'' AND account_blocked=0 AND "
               "(oauth_validated_at IS NULL OR oauth_validated_at<=UTC_TIMESTAMP()-INTERVAL 1 HOUR OR "
               "twitch_token_expires_at<=DATE_FORMAT(UTC_TIMESTAMP()+INTERVAL 1 "
               "MINUTE,'%Y-%m-%dT%H:%i:%sZ')) ORDER BY id LIMIT " +
               std::to_string(std::min(limit, 100u));
    database::query(conn.raw(), sql.c_str());
    database::Result result(mysql_store_result(conn.raw()));
    std::vector<models::User> out;
    while (auto row = mysql_fetch_row(result))
        out.push_back(row_to_user(row, result));
    return out;
}
void UserRepository::update_password_hash(uint64_t id, const std::string &hash) {
    database::ScopedConnection conn(pool_);
    auto sql = "UPDATE users SET password_hash='" + database::escape(conn.raw(), hash) +
               "' WHERE id=" + std::to_string(id) + " AND twitch_id IS NULL";
    database::query(conn.raw(), sql.c_str());
}
void UserRepository::revoke_sessions(uint64_t id) {
    database::ScopedConnection conn(pool_);
    auto q = "UPDATE users SET auth_version=auth_version+1 WHERE id=" + std::to_string(id);
    database::query(conn.raw(), q.c_str());
}
void UserRepository::mark_oauth_validated(uint64_t id) {
    database::ScopedConnection conn(pool_);
    auto sql = "UPDATE users SET oauth_validated_at=UTC_TIMESTAMP() WHERE id=" + std::to_string(id);
    database::query(conn.raw(), sql.c_str());
}
} // namespace clipstream::repositories
