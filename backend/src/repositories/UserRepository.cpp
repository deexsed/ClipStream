#include "repositories/UserRepository.hpp"
#include "database/SqlEscape.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace clipstream::repositories {

UserRepository::UserRepository(database::ConnectionPool& pool) : pool_(pool) {}

models::User UserRepository::row_to_user(MYSQL_ROW row, MYSQL_RES* res) {
    auto field_idx = [&](const char* name) -> int {
        unsigned int num = mysql_num_fields(res);
        MYSQL_FIELD* fields = mysql_fetch_fields(res);
        for (unsigned int i = 0; i < num; ++i)
            if (std::strcmp(fields[i].name, name) == 0) return static_cast<int>(i);
        return -1;
    };

    models::User u;
    u.id            = std::stoull(row[field_idx("id")]);
    u.username      = row[field_idx("username")] ? row[field_idx("username")] : "";
    u.email         = row[field_idx("email")]    ? row[field_idx("email")]    : "";
    u.password_hash = row[field_idx("password_hash")] ? row[field_idx("password_hash")] : "";
    u.avatar_url    = row[field_idx("avatar_url")]    ? row[field_idx("avatar_url")]    : "";
    u.twitch_id     = row[field_idx("twitch_id")]     ? row[field_idx("twitch_id")]     : "";
    int oat = field_idx("twitch_access_token");
    if (oat >= 0 && row[oat]) u.twitch_access_token = row[oat];
    int ort = field_idx("twitch_refresh_token");
    if (ort >= 0 && row[ort]) u.twitch_refresh_token = row[ort];
    int oex = field_idx("twitch_token_expires_at");
    if (oex >= 0 && row[oex]) u.twitch_token_expires_at = row[oex];
    u.reputation    = std::stoi(row[field_idx("reputation")]);
    u.created_at    = row[field_idx("created_at")] ? row[field_idx("created_at")] : "";

    std::string role_str = row[field_idx("role")] ? row[field_idx("role")] : "user";
    if (role_str == "admin") u.role = models::UserRole::Admin;
    else if (role_str == "moderator") u.role = models::UserRole::Moderator;
    else if (role_str == "vip") u.role = models::UserRole::Vip;
    else u.role = models::UserRole::User;

    int ab = field_idx("account_blocked");
    if (ab >= 0 && row[ab]) u.account_blocked = (std::stoi(row[ab]) != 0);
    int cb = field_idx("chat_banned_until");
    if (cb >= 0 && row[cb]) u.chat_banned_until = std::string(row[cb]);

    int lcv = field_idx("legal_consent_version");
    if (lcv >= 0 && row[lcv]) u.legal_consent_version = row[lcv];
    int lca = field_idx("legal_consent_at");
    if (lca >= 0 && row[lca]) u.legal_consent_at = row[lca];

    std::string badge_str = row[field_idx("badge")] ? row[field_idx("badge")] : "none";
    if (badge_str == "legend")          u.badge = models::Badge::Legend;
    else if (badge_str == "pro")        u.badge = models::Badge::Pro;
    else if (badge_str == "rising_star") u.badge = models::Badge::RisingStar;
    else                                 u.badge = models::Badge::None;

    return u;
}

std::optional<models::User> UserRepository::find_by_id(uint64_t id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT * FROM users WHERE id = " + std::to_string(id);
    if (mysql_query(conn.raw(), sql.c_str())) return std::nullopt;

    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) { mysql_free_result(res); return std::nullopt; }

    auto user = row_to_user(row, res);
    mysql_free_result(res);
    return user;
}

std::vector<models::User> UserRepository::find_all(uint32_t limit, uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT * FROM users ORDER BY id LIMIT "
        + std::to_string(limit) + " OFFSET " + std::to_string(offset);
    if (mysql_query(conn.raw(), sql.c_str())) return {};

    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return {};

    std::vector<models::User> users;
    while (MYSQL_ROW row = mysql_fetch_row(res))
        users.push_back(row_to_user(row, res));

    mysql_free_result(res);
    return users;
}

uint64_t UserRepository::create(const models::User& user) {
    database::ScopedConnection conn(pool_);

    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };

    std::string sql = "INSERT INTO users (username, email, password_hash, avatar_url, twitch_id) VALUES ('"
        + esc(user.username) + "', '" + esc(user.email) + "', '" + esc(user.password_hash)
        + "', '" + esc(user.avatar_url) + "', '" + esc(user.twitch_id) + "')";

    if (mysql_query(conn.raw(), sql.c_str()))
        throw std::runtime_error(std::string("Create user failed: ") + mysql_error(conn.raw()));

    return mysql_insert_id(conn.raw());
}

bool UserRepository::update(const models::User& user) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    std::string sql = "UPDATE users SET username='" + esc(user.username)
        + "', avatar_url='" + esc(user.avatar_url)
        + "' WHERE id=" + std::to_string(user.id);
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

bool UserRepository::remove(uint64_t id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "DELETE FROM users WHERE id=" + std::to_string(id);
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

std::optional<models::User> UserRepository::find_by_email(const std::string& email) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    std::string sql = "SELECT * FROM users WHERE email='" + esc(email) + "'";
    if (mysql_query(conn.raw(), sql.c_str())) return std::nullopt;

    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) { mysql_free_result(res); return std::nullopt; }

    auto user = row_to_user(row, res);
    mysql_free_result(res);
    return user;
}

std::optional<models::User> UserRepository::find_by_username(const std::string& username) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    std::string sql = "SELECT * FROM users WHERE username='" + esc(username) + "'";
    if (mysql_query(conn.raw(), sql.c_str())) return std::nullopt;

    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) { mysql_free_result(res); return std::nullopt; }

    auto user = row_to_user(row, res);
    mysql_free_result(res);
    return user;
}

std::optional<models::User> UserRepository::find_by_twitch_id(const std::string& twitch_id) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    std::string sql = "SELECT * FROM users WHERE twitch_id='" + esc(twitch_id) + "'";
    if (mysql_query(conn.raw(), sql.c_str())) return std::nullopt;

    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) { mysql_free_result(res); return std::nullopt; }

    auto user = row_to_user(row, res);
    mysql_free_result(res);
    return user;
}

bool UserRepository::update_reputation(uint64_t id, int delta) {
    if (id == 0 || delta == 0) return true;
    database::ScopedConnection conn(pool_);
    std::string sql = "UPDATE users SET reputation = GREATEST(0, reputation + (" + std::to_string(delta)
        + ")) WHERE id=" + std::to_string(id);
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

uint64_t UserRepository::clip_owner_user_id_for_reputation(const models::Clip& clip) {
    if (clip.author_id != 0) return clip.author_id;
    if (!clip.creator_twitch_id.empty()) {
        if (auto u = find_by_twitch_id(clip.creator_twitch_id)) return u->id;
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

    auto one_int = [&](const std::string& sql) -> int {
        if (mysql_query(conn.raw(), sql.c_str())) return 0;
        MYSQL_RES* res = mysql_store_result(conn.raw());
        if (!res) return 0;
        MYSQL_ROW row = mysql_fetch_row(res);
        int out = (row && row[0]) ? std::stoi(row[0]) : 0;
        mysql_free_result(res);
        return out;
    };

    stats["liked_count"] = one_int("SELECT COUNT(*) FROM likes WHERE user_id=" + std::to_string(user_id) + " AND is_like=1");
    stats["disliked_count"] = one_int("SELECT COUNT(*) FROM likes WHERE user_id=" + std::to_string(user_id) + " AND is_like=0");
    stats["unique_viewed_clips"] = one_int("SELECT COUNT(DISTINCT clip_id) FROM view_history WHERE user_id=" + std::to_string(user_id));
    stats["total_views_events"] = one_int("SELECT COUNT(*) FROM view_history WHERE user_id=" + std::to_string(user_id));
    stats["comments_count"] = one_int("SELECT COUNT(*) FROM comments WHERE user_id=" + std::to_string(user_id));
    stats["friends_count"]   = one_int("SELECT COUNT(*) FROM friendships WHERE user_id=" + std::to_string(user_id));
    stats["following_count"] = 0;
    stats["followers_count"] = 0;
    stats["clips_created_count"] = one_int(
        "SELECT COUNT(*) FROM clips WHERE unavailable_at IS NULL AND (author_id=" + std::to_string(user_id)
        + " OR (clip_creator_twitch_id<>'' AND clip_creator_twitch_id=(SELECT twitch_id FROM users WHERE id="
        + std::to_string(user_id) + " LIMIT 1)))");
    stats["night_views"] = one_int(
        "SELECT COUNT(*) FROM view_history WHERE user_id=" + std::to_string(user_id)
        + " AND (HOUR(viewed_at) BETWEEN 0 AND 5)");
    stats["wall_posts_count"] = one_int(
        "SELECT COUNT(*) FROM profile_wall_posts WHERE profile_owner_id=" + std::to_string(user_id));

    {
        std::string sql =
            "SELECT vh.viewed_at FROM view_history vh WHERE vh.user_id=" + std::to_string(user_id)
            + " ORDER BY vh.viewed_at DESC LIMIT 1";
        if (mysql_query(conn.raw(), sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn.raw());
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0]) stats["last_view_at"] = std::string(row[0]);
                mysql_free_result(res);
            }
        }
    }

    {
        std::string sql =
            "SELECT c.game_id, MAX(g.name) AS game_name, COUNT(*) AS cnt "
            "FROM view_history vh "
            "JOIN clips c ON c.id=vh.clip_id "
            "LEFT JOIN games g ON g.id=c.game_id "
            "WHERE vh.user_id=" + std::to_string(user_id) + " AND c.game_id IS NOT NULL "
            "GROUP BY c.game_id "
            "ORDER BY cnt DESC LIMIT 1";
        if (mysql_query(conn.raw(), sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn.raw());
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0]) {
                    stats["favorite_game_id"] = std::stoull(row[0]);
                    stats["favorite_game_name"] = row[1] ? std::string(row[1]) : "";
                }
                mysql_free_result(res);
            }
        }
    }

    {
        std::string sql =
            "SELECT c.broadcaster_id, MAX(c.broadcaster_name) AS broadcaster_name, COUNT(*) AS cnt "
            "FROM view_history vh "
            "JOIN clips c ON c.id=vh.clip_id "
            "WHERE vh.user_id=" + std::to_string(user_id) + " AND c.broadcaster_id<>'' "
            "GROUP BY c.broadcaster_id "
            "ORDER BY cnt DESC LIMIT 1";
        if (mysql_query(conn.raw(), sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn.raw());
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0]) {
                    stats["favorite_broadcaster_id"] = std::string(row[0]);
                    stats["favorite_broadcaster_name"] = row[1] ? std::string(row[1]) : "";
                }
                mysql_free_result(res);
            }
        }
    }

    return stats;
}

bool UserRepository::update_badge(uint64_t user_id, models::Badge badge) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    auto badge_str = [&](models::Badge b) -> std::string {
        switch (b) {
            case models::Badge::RisingStar: return "rising_star";
            case models::Badge::Pro: return "pro";
            case models::Badge::Legend: return "legend";
            case models::Badge::None:
            default: return "none";
        }
    };
    std::string sel = "SELECT badge FROM users WHERE id=" + std::to_string(user_id) + " LIMIT 1";
    if (mysql_query(conn.raw(), sel.c_str()) != 0) return false;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return false;
    MYSQL_ROW row = mysql_fetch_row(res);
    std::string old_badge = (row && row[0]) ? std::string(row[0]) : "none";
    mysql_free_result(res);

    const std::string new_badge = badge_str(badge);
    if (old_badge == new_badge) return true;

    if (old_badge != "none") {
        std::string h = "INSERT INTO user_badge_history (user_id, badge, event_type) VALUES ("
            + std::to_string(user_id) + ",'" + esc(old_badge) + "','revoked')";
        mysql_query(conn.raw(), h.c_str());
    }
    if (new_badge != "none") {
        std::string h = "INSERT INTO user_badge_history (user_id, badge, event_type) VALUES ("
            + std::to_string(user_id) + ",'" + esc(new_badge) + "','granted')";
        mysql_query(conn.raw(), h.c_str());
    }
    std::string sql = "UPDATE users SET badge='" + esc(new_badge) + "' WHERE id=" + std::to_string(user_id);
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

bool UserRepository::update_twitch_oauth(uint64_t id, const std::string& access_token,
                                         const std::string& refresh_token,
                                         const std::string& expires_at_iso) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    std::string sql = "UPDATE users SET twitch_access_token='" + esc(access_token)
        + "', twitch_refresh_token='" + esc(refresh_token)
        + "', twitch_token_expires_at='" + esc(expires_at_iso)
        + "' WHERE id=" + std::to_string(id);
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

bool UserRepository::clear_twitch_oauth_by_twitch_id(const std::string& twitch_id) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    std::string sql = "UPDATE users SET twitch_access_token=NULL, twitch_refresh_token=NULL, twitch_token_expires_at=NULL "
                      "WHERE twitch_id='" + esc(twitch_id) + "'";
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

bool UserRepository::set_legal_consent(uint64_t user_id, const std::string& version, const std::string& accepted_at_iso) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    std::string sql = "UPDATE users SET legal_consent_version='" + esc(version)
        + "', legal_consent_at='" + esc(accepted_at_iso) + "' WHERE id=" + std::to_string(user_id);
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

namespace {

std::string role_to_db(models::UserRole r) {
    switch (r) {
        case models::UserRole::Admin: return "admin";
        case models::UserRole::Moderator: return "moderator";
        case models::UserRole::Vip: return "vip";
        default: return "user";
    }
}

} // namespace

uint32_t UserRepository::count_by_role(models::UserRole role) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT COUNT(*) FROM users WHERE `role`='" + role_to_db(role) + "'";
    if (mysql_query(conn.raw(), sql.c_str())) return 0;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    uint32_t n = 0;
    if (row && row[0]) n = static_cast<uint32_t>(std::stoul(row[0]));
    mysql_free_result(res);
    return n;
}

bool UserRepository::update_role(uint64_t user_id, models::UserRole role) {
    database::ScopedConnection conn(pool_);
    std::string sql =
        "UPDATE users SET `role`='" + role_to_db(role) + "' WHERE id=" + std::to_string(user_id);
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

nlohmann::json UserRepository::get_admin_overview_stats() {
    database::ScopedConnection conn(pool_);
    const char* sql =
        "SELECT "
        "(SELECT COUNT(*) FROM users) AS users_total, "
        "(SELECT COUNT(*) FROM users WHERE `role`='admin') AS users_admin, "
        "(SELECT COUNT(*) FROM users WHERE `role`='moderator') AS users_moderator, "
        "(SELECT COUNT(*) FROM users WHERE `role`='vip') AS users_vip, "
        "(SELECT COUNT(*) FROM users WHERE `role`='user') AS users_regular, "
        "(SELECT COUNT(*) FROM clips) AS clips_total, "
        "(SELECT COUNT(*) FROM comments) AS comments_total, "
        "(SELECT COUNT(*) FROM view_history WHERE viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 24 HOUR)) AS "
        "views_24h";
    nlohmann::json out = nlohmann::json::object();
    if (mysql_query(conn.raw(), sql) != 0) return out;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return out;
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
    mysql_free_result(res);
    return out;
}

std::vector<nlohmann::json> UserRepository::list_users_admin(uint32_t limit, uint32_t offset,
                                                             const std::string& search) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };

    std::string where = "1=1";
    if (!search.empty()) {
        std::string q = esc(search);
        where += " AND (username LIKE '%" + q + "%' OR email LIKE '%" + q + "%')";
    }
    std::string sql =
        "SELECT id, username, email, avatar_url, `role`, reputation, created_at FROM users WHERE " + where
        + " ORDER BY id DESC LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);

    std::vector<nlohmann::json> out;
    if (mysql_query(conn.raw(), sql.c_str())) return out;

    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return out;

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
    mysql_free_result(res);
    return out;
}

uint64_t UserRepository::count_users_admin(const std::string& search) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };

    std::string where = "1=1";
    if (!search.empty()) {
        std::string q = esc(search);
        where += " AND (username LIKE '%" + q + "%' OR email LIKE '%" + q + "%')";
    }
    std::string sql = "SELECT COUNT(*) FROM users WHERE " + where;
    if (mysql_query(conn.raw(), sql.c_str())) return 0;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    uint64_t n = 0;
    if (row && row[0]) n = std::stoull(row[0]);
    mysql_free_result(res);
    return n;
}

bool UserRepository::may_post_comment(uint64_t user_id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT COUNT(*) FROM users WHERE id=" + std::to_string(user_id)
        + " AND account_blocked=0 AND (chat_banned_until IS NULL OR chat_banned_until<=UTC_TIMESTAMP())";
    if (mysql_query(conn.raw(), sql.c_str())) return false;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return false;
    MYSQL_ROW row = mysql_fetch_row(res);
    bool ok = row && row[0] && std::stoul(row[0]) > 0;
    mysql_free_result(res);
    return ok;
}

bool UserRepository::apply_chat_ban(uint64_t user_id, bool permanent, uint32_t days, const std::string& reason,
                                    uint64_t moderator_id) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    std::string until_sql;
    if (permanent) until_sql = "'2099-12-31 23:59:59'";
    else until_sql = "DATE_ADD(UTC_TIMESTAMP(), INTERVAL " + std::to_string(std::max(1u, days)) + " DAY)";
    std::string sql = "UPDATE users SET chat_banned_until=" + until_sql + ",chat_ban_reason='" + esc(reason)
        + "',chat_banned_by=" + std::to_string(moderator_id) + " WHERE id=" + std::to_string(user_id);
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

bool UserRepository::clear_chat_ban(uint64_t user_id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "UPDATE users SET chat_banned_until=NULL,chat_ban_reason=NULL,chat_banned_by=NULL WHERE id="
        + std::to_string(user_id);
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

bool UserRepository::set_account_blocked(uint64_t user_id, bool blocked) {
    database::ScopedConnection conn(pool_);
    std::string sql =
        "UPDATE users SET account_blocked=" + std::string(blocked ? "1" : "0") + " WHERE id=" + std::to_string(user_id);
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

bool UserRepository::follow(uint64_t follower_id, uint64_t followed_id) {
    return try_follow(follower_id, followed_id) >= 0;
}

int UserRepository::try_follow(uint64_t follower_id, uint64_t followed_id) {
    if (follower_id == followed_id) return 0;
    database::ScopedConnection conn(pool_);
    std::string sql = "INSERT IGNORE INTO subscriptions (follower_id, followed_id) VALUES ("
        + std::to_string(follower_id) + "," + std::to_string(followed_id) + ")";
    if (mysql_query(conn.raw(), sql.c_str()) != 0) return -1;
    return mysql_affected_rows(conn.raw()) == 1 ? 1 : 0;
}

std::unordered_set<uint64_t> UserRepository::following_ids_among(uint64_t follower_id,
                                                                 const std::vector<uint64_t>& user_ids) {
    std::unordered_set<uint64_t> out;
    if (follower_id == 0 || user_ids.empty()) return out;
    database::ScopedConnection conn(pool_);
    std::ostringstream in;
    for (size_t i = 0; i < user_ids.size(); ++i) {
        if (i) in << ",";
        in << user_ids[i];
    }
    std::string sql = "SELECT friend_id FROM friendships WHERE user_id=" + std::to_string(follower_id)
        + " AND friend_id IN (" + in.str() + ")";
    if (mysql_query(conn.raw(), sql.c_str())) return out;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        if (row[0]) out.insert(std::stoull(row[0]));
    }
    mysql_free_result(res);
    return out;
}

bool UserRepository::unfollow(uint64_t follower_id, uint64_t followed_id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "DELETE FROM subscriptions WHERE follower_id=" + std::to_string(follower_id)
        + " AND followed_id=" + std::to_string(followed_id);
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

bool UserRepository::is_following(uint64_t follower_id, uint64_t followed_id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT 1 FROM friendships WHERE user_id=" + std::to_string(follower_id)
        + " AND friend_id=" + std::to_string(followed_id) + " LIMIT 1";
    if (mysql_query(conn.raw(), sql.c_str()) != 0) return false;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return false;
    bool ok = mysql_fetch_row(res) != nullptr;
    mysql_free_result(res);
    return ok;
}

nlohmann::json UserRepository::get_user_settings(uint64_t user_id) {
    database::ScopedConnection conn(pool_);
    nlohmann::json out = {{"hide_mature", true},
                          {"mute_keywords", nlohmann::json::array()},
                          {"clip_languages", nlohmann::json::array()}};
    std::string sql =
        "SELECT hide_mature, mute_keywords, clip_languages FROM user_settings WHERE user_id=" + std::to_string(user_id)
        + " LIMIT 1";
    if (mysql_query(conn.raw(), sql.c_str()) != 0) return out;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return out;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        if (row[0]) out["hide_mature"] = (std::stoi(row[0]) != 0);
        if (row[1]) {
            try {
                auto j = nlohmann::json::parse(row[1]);
                if (j.is_array()) out["mute_keywords"] = std::move(j);
            } catch (...) {}
        }
        if (row[2]) {
            try {
                auto j = nlohmann::json::parse(row[2]);
                if (j.is_array()) out["clip_languages"] = std::move(j);
            } catch (...) {}
        }
    }
    mysql_free_result(res);
    return out;
}

bool UserRepository::save_user_settings(uint64_t user_id, bool hide_mature,
                                       const std::vector<std::string>& mute_keywords,
                                       const std::vector<std::string>& clip_languages) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : mute_keywords) {
        if (s.empty()) continue;
        std::string t = s.size() > 64 ? s.substr(0, 64) : s;
        arr.push_back(t);
        if (arr.size() >= 32) break;
    }
    nlohmann::json lang_arr = nlohmann::json::array();
    for (const auto& s : clip_languages) {
        std::string norm;
        for (char ch : s) {
            if (ch >= 'A' && ch <= 'Z') norm += static_cast<char>(ch - 'A' + 'a');
            else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')
                norm += ch;
            if (norm.size() >= 16) break;
        }
        if (!norm.empty()) {
            lang_arr.push_back(norm);
            if (lang_arr.size() >= 24) break;
        }
    }
    const std::string jdump      = esc(arr.dump());
    const std::string jlang        = esc(lang_arr.dump());
    std::string       sql =
        "INSERT INTO user_settings (user_id, hide_mature, mute_keywords, clip_languages) VALUES ("
        + std::to_string(user_id) + "," + std::string(hide_mature ? "1" : "0") + ",'" + jdump + "','" + jlang + "') "
          "ON DUPLICATE KEY UPDATE hide_mature=VALUES(hide_mature), mute_keywords=VALUES(mute_keywords), "
          "clip_languages=VALUES(clip_languages)";
    return mysql_query(conn.raw(), sql.c_str()) == 0;
}

std::vector<nlohmann::json> UserRepository::get_badge_history(uint64_t user_id, uint32_t limit) {
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT id, badge, event_type, created_at FROM user_badge_history WHERE user_id="
        + std::to_string(user_id) + " ORDER BY created_at DESC LIMIT " + std::to_string(std::max(1u, limit));
    if (mysql_query(conn.raw(), sql.c_str())) return {};
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return {};
    std::vector<nlohmann::json> out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"badge", row[1] ? std::string(row[1]) : ""},
            {"event_type", row[2] ? std::string(row[2]) : ""},
            {"created_at", row[3] ? std::string(row[3]) : ""},
        });
    }
    mysql_free_result(res);
    return out;
}

std::optional<nlohmann::json> UserRepository::get_public_profile(const std::string& username, uint64_t viewer_id) {
    auto u = find_by_username(username);
    if (!u) return std::nullopt;
    nlohmann::json j = {
        {"id", u->id},
        {"username", u->username},
        {"avatar_url", u->avatar_url},
        {"reputation", u->reputation},
        {"badge", static_cast<int>(u->badge)},
        {"created_at", u->created_at},
    };
    if (!u->twitch_id.empty()) j["twitch_id"] = u->twitch_id;
    else j["twitch_id"] = nullptr;
    j["stats"] = get_profile_stats(u->id);
    j["badge_history"] = get_badge_history(u->id, 50);
    j["friendship"]    = friendship_status(viewer_id, u->id);
    {
        auto ach = list_earned_achievements(u->id, 64);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& row : ach) arr.push_back(row);
        j["achievements_earned"] = std::move(arr);
    }
    return j;
}

bool UserRepository::has_recent_site_activity(uint64_t user_id, unsigned window_minutes) {
    if (user_id == 0) return false;
    unsigned w = std::max(1u, std::min(120u, window_minutes));
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT 1 FROM analytics_sessions WHERE user_id=" + std::to_string(user_id)
        + " AND last_seen_at >= UTC_TIMESTAMP() - INTERVAL " + std::to_string(w) + " MINUTE LIMIT 1";
    if (mysql_query(conn.raw(), sql.c_str())) return false;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return false;
    const bool ok = mysql_fetch_row(res) != nullptr;
    mysql_free_result(res);
    return ok;
}

std::optional<nlohmann::json> UserRepository::find_site_user_by_twitch_id(const std::string& twitch_id) {
    if (twitch_id.empty()) return std::nullopt;
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    std::string sql = "SELECT id, username FROM users WHERE twitch_id='" + esc(twitch_id)
        + "' AND account_blocked=0 LIMIT 1";
    if (mysql_query(conn.raw(), sql.c_str())) return std::nullopt;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return std::nullopt;
    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<nlohmann::json> out;
    if (row && row[0] && row[1]) {
        out = nlohmann::json{
            {"id", std::stoull(row[0])},
            {"username", std::string(row[1])},
        };
    }
    mysql_free_result(res);
    return out;
}

std::vector<nlohmann::json> UserRepository::list_followers(uint64_t user_id, uint32_t limit, uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT u.id, u.username, u.avatar_url, s.created_at AS followed_since "
        "FROM subscriptions s "
        "INNER JOIN users u ON u.id = s.follower_id "
        "WHERE s.followed_id = " + std::to_string(user_id)
        + " AND u.account_blocked = 0 "
          "ORDER BY s.created_at DESC LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
    if (mysql_query(conn.raw(), sql.c_str())) return {};
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return {};
    std::vector<nlohmann::json> out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"username", row[1] ? std::string(row[1]) : ""},
            {"avatar_url", row[2] ? std::string(row[2]) : ""},
            {"since", row[3] ? std::string(row[3]) : ""},
        });
    }
    mysql_free_result(res);
    return out;
}

std::vector<nlohmann::json> UserRepository::list_following(uint64_t user_id, uint32_t limit, uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT u.id, u.username, u.avatar_url, s.created_at AS followed_since "
        "FROM subscriptions s "
        "INNER JOIN users u ON u.id = s.followed_id "
        "WHERE s.follower_id = " + std::to_string(user_id)
        + " AND u.account_blocked = 0 "
          "ORDER BY s.created_at DESC LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
    if (mysql_query(conn.raw(), sql.c_str())) return {};
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return {};
    std::vector<nlohmann::json> out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"username", row[1] ? std::string(row[1]) : ""},
            {"avatar_url", row[2] ? std::string(row[2]) : ""},
            {"since", row[3] ? std::string(row[3]) : ""},
        });
    }
    mysql_free_result(res);
    return out;
}

std::vector<nlohmann::json> UserRepository::search_users_by_username_prefix(const std::string& query,
                                                                             uint32_t limit) {
    if (query.size() < 2) return {};
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    std::string q = esc(query);
    std::string sql =
        "SELECT id, username, avatar_url FROM users WHERE account_blocked = 0 AND username LIKE '%" + q
        + "%' ORDER BY username ASC LIMIT " + std::to_string(std::max(1u, std::min(50u, limit)));
    if (mysql_query(conn.raw(), sql.c_str())) return {};
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return {};
    std::vector<nlohmann::json> out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"username", row[1] ? std::string(row[1]) : ""},
            {"avatar_url", row[2] ? std::string(row[2]) : ""},
        });
    }
    mysql_free_result(res);
    return out;
}

std::string UserRepository::friendship_status(uint64_t viewer_id, uint64_t target_id) {
    if (viewer_id == 0) return "none";
    if (viewer_id == target_id) return "self";
    std::vector<uint64_t> v{target_id};
    auto m = friendship_status_bulk(viewer_id, v);
    auto it = m.find(target_id);
    return it == m.end() ? "none" : it->second;
}

std::unordered_map<uint64_t, std::string> UserRepository::friendship_status_bulk(uint64_t viewer_id,
                                                                                  const std::vector<uint64_t>& target_ids) {
    std::unordered_map<uint64_t, std::string> out;
    if (viewer_id == 0 || target_ids.empty()) return out;
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
    if (ids.empty()) return out;
    database::ScopedConnection conn(pool_);
    auto in_list = [&](const std::vector<uint64_t>& v) {
        std::ostringstream o;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) o << ",";
            o << v[i];
        }
        return o.str();
    };
    const std::string in = in_list(ids);
    {
        std::string sql = "SELECT friend_id FROM friendships WHERE user_id=" + std::to_string(viewer_id)
            + " AND friend_id IN (" + in + ")";
        if (mysql_query(conn.raw(), sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn.raw());
            if (res) {
                while (MYSQL_ROW row = mysql_fetch_row(res)) {
                    if (row[0]) out[std::stoull(row[0])] = "friends";
                }
                mysql_free_result(res);
            }
        }
    }
    std::vector<uint64_t> rest;
    for (uint64_t t : ids) {
        if (out[t] != "friends") rest.push_back(t);
    }
    if (rest.empty()) return out;
    const std::string in2 = in_list(rest);
    {
        std::string sql = "SELECT addressee_id FROM friend_requests WHERE requester_id=" + std::to_string(viewer_id)
            + " AND addressee_id IN (" + in2 + ")";
        if (mysql_query(conn.raw(), sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn.raw());
            if (res) {
                while (MYSQL_ROW row = mysql_fetch_row(res)) {
                    if (row[0]) out[std::stoull(row[0])] = "outgoing";
                }
                mysql_free_result(res);
            }
        }
    }
    {
        std::string sql = "SELECT requester_id FROM friend_requests WHERE addressee_id=" + std::to_string(viewer_id)
            + " AND requester_id IN (" + in2 + ")";
        if (mysql_query(conn.raw(), sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn.raw());
            if (res) {
                while (MYSQL_ROW row = mysql_fetch_row(res)) {
                    if (row[0]) {
                        uint64_t rid = std::stoull(row[0]);
                        if (out[rid] == "none") out[rid] = "incoming";
                    }
                }
                mysql_free_result(res);
            }
        }
    }
    return out;
}

static bool mysql_exec_ok(MYSQL* raw, const std::string& sql) { return mysql_query(raw, sql.c_str()) == 0; }

int UserRepository::send_friend_request(uint64_t requester_id, uint64_t addressee_id) {
    if (requester_id == 0 || addressee_id == 0 || requester_id == addressee_id) return -1;
    database::ScopedConnection conn(pool_);
    std::string q1 = "SELECT 1 FROM friendships WHERE user_id=" + std::to_string(requester_id) + " AND friend_id="
        + std::to_string(addressee_id) + " LIMIT 1";
    if (mysql_query(conn.raw(), q1.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn.raw());
        bool exists = res && mysql_fetch_row(res);
        if (res) mysql_free_result(res);
        if (exists) return 0;
    }
    std::string qrev = "SELECT 1 FROM friend_requests WHERE requester_id=" + std::to_string(addressee_id)
        + " AND addressee_id=" + std::to_string(requester_id) + " LIMIT 1";
    if (mysql_query(conn.raw(), qrev.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn.raw());
        bool rev = res && mysql_fetch_row(res);
        if (res) mysql_free_result(res);
        if (rev) return accept_friend_request(requester_id, addressee_id) ? 2 : -1;
    }
    std::string ins = "INSERT IGNORE INTO friend_requests (requester_id, addressee_id) VALUES ("
        + std::to_string(requester_id) + "," + std::to_string(addressee_id) + ")";
    if (!mysql_exec_ok(conn.raw(), ins)) return -1;
    return mysql_affected_rows(conn.raw()) == 1 ? 1 : 0;
}

bool UserRepository::cancel_friend_request(uint64_t requester_id, uint64_t addressee_id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "DELETE FROM friend_requests WHERE requester_id=" + std::to_string(requester_id)
        + " AND addressee_id=" + std::to_string(addressee_id);
    return mysql_exec_ok(conn.raw(), sql);
}

bool UserRepository::accept_friend_request(uint64_t addressee_id, uint64_t requester_id) {
    if (addressee_id == 0 || requester_id == 0) return false;
    database::ScopedConnection conn(pool_);
    std::string del = "DELETE FROM friend_requests WHERE requester_id=" + std::to_string(requester_id)
        + " AND addressee_id=" + std::to_string(addressee_id);
    if (!mysql_exec_ok(conn.raw(), del) || mysql_affected_rows(conn.raw()) == 0) return false;
    std::string a = "INSERT IGNORE INTO friendships (user_id, friend_id) VALUES ("
        + std::to_string(addressee_id) + "," + std::to_string(requester_id) + "),(" + std::to_string(requester_id) + ","
        + std::to_string(addressee_id) + ")";
    return mysql_exec_ok(conn.raw(), a);
}

bool UserRepository::decline_friend_request(uint64_t addressee_id, uint64_t requester_id) {
    return cancel_friend_request(requester_id, addressee_id);
}

bool UserRepository::remove_friendship(uint64_t user_a, uint64_t user_b) {
    if (user_a == 0 || user_b == 0) return false;
    database::ScopedConnection conn(pool_);
    std::string sql = "DELETE FROM friendships WHERE (user_id=" + std::to_string(user_a) + " AND friend_id="
        + std::to_string(user_b) + ") OR (user_id=" + std::to_string(user_b) + " AND friend_id=" + std::to_string(user_a)
        + ")";
    return mysql_exec_ok(conn.raw(), sql);
}

std::vector<nlohmann::json> UserRepository::list_friends(uint64_t user_id, uint32_t limit, uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT u.id, u.username, u.avatar_url, f.created_at AS friends_since "
        "FROM friendships f INNER JOIN users u ON u.id = f.friend_id "
        "WHERE f.user_id = " + std::to_string(user_id)
        + " AND u.account_blocked = 0 ORDER BY f.created_at DESC LIMIT " + std::to_string(limit) + " OFFSET "
        + std::to_string(offset);
    std::vector<nlohmann::json> out;
    if (mysql_query(conn.raw(), sql.c_str())) return out;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"username", row[1] ? std::string(row[1]) : ""},
            {"avatar_url", row[2] ? std::string(row[2]) : ""},
            {"since", row[3] ? std::string(row[3]) : ""},
        });
    }
    mysql_free_result(res);
    return out;
}

std::vector<nlohmann::json> UserRepository::list_friend_requests_incoming(uint64_t user_id, uint32_t limit,
                                                                          uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT u.id, u.username, u.avatar_url, r.created_at AS requested_at "
        "FROM friend_requests r INNER JOIN users u ON u.id = r.requester_id "
        "WHERE r.addressee_id = " + std::to_string(user_id)
        + " AND u.account_blocked = 0 ORDER BY r.created_at DESC LIMIT " + std::to_string(limit) + " OFFSET "
        + std::to_string(offset);
    std::vector<nlohmann::json> out;
    if (mysql_query(conn.raw(), sql.c_str())) return out;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"username", row[1] ? std::string(row[1]) : ""},
            {"avatar_url", row[2] ? std::string(row[2]) : ""},
            {"since", row[3] ? std::string(row[3]) : ""},
        });
    }
    mysql_free_result(res);
    return out;
}

std::vector<nlohmann::json> UserRepository::list_friend_requests_outgoing(uint64_t user_id, uint32_t limit,
                                                                          uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT u.id, u.username, u.avatar_url, r.created_at AS requested_at "
        "FROM friend_requests r INNER JOIN users u ON u.id = r.addressee_id "
        "WHERE r.requester_id = " + std::to_string(user_id)
        + " AND u.account_blocked = 0 ORDER BY r.created_at DESC LIMIT " + std::to_string(limit) + " OFFSET "
        + std::to_string(offset);
    std::vector<nlohmann::json> out;
    if (mysql_query(conn.raw(), sql.c_str())) return out;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"username", row[1] ? std::string(row[1]) : ""},
            {"avatar_url", row[2] ? std::string(row[2]) : ""},
            {"since", row[3] ? std::string(row[3]) : ""},
        });
    }
    mysql_free_result(res);
    return out;
}

bool UserRepository::sync_earned_achievements(uint64_t user_id, const std::vector<std::string>& keys) {
    if (user_id == 0 || keys.empty()) return true;
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    for (const auto& k : keys) {
        if (k.empty() || k.size() > 64) continue;
        std::string sql = "INSERT IGNORE INTO user_achievements (user_id, achievement_key) VALUES ("
            + std::to_string(user_id) + ",'" + esc(k) + "')";
        mysql_query(conn.raw(), sql.c_str());
    }
    return true;
}

std::vector<nlohmann::json> UserRepository::list_earned_achievements(uint64_t user_id, uint32_t limit) {
    if (limit < 1) limit = 1;
    if (limit > 128) limit = 128;
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT achievement_key, earned_at FROM user_achievements WHERE user_id="
        + std::to_string(user_id) + " ORDER BY earned_at DESC LIMIT " + std::to_string(limit);
    std::vector<nlohmann::json> out;
    if (mysql_query(conn.raw(), sql.c_str())) return out;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"key", row[0] ? std::string(row[0]) : ""},
            {"earned_at", row[1] ? std::string(row[1]) : ""},
        });
    }
    mysql_free_result(res);
    return out;
}

std::vector<nlohmann::json> UserRepository::list_wall_posts(uint64_t profile_owner_id, uint32_t limit, uint32_t offset) {
    if (limit < 1) limit = 1;
    if (limit > 50) limit = 50;
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT p.id, p.body, p.created_at, u.id AS aid, u.username AS aname, u.avatar_url AS aavatar "
        "FROM profile_wall_posts p INNER JOIN users u ON u.id=p.author_id "
        "WHERE p.profile_owner_id=" + std::to_string(profile_owner_id)
        + " ORDER BY p.created_at DESC LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
    std::vector<nlohmann::json> out;
    struct Row {
        uint64_t    pid{};
        std::string body;
        std::string created_at;
        uint64_t    aid{};
        std::string aname;
        std::string aavatar;
    };
    std::vector<Row> rows;
    if (mysql_query(conn.raw(), sql.c_str())) return out;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        Row r;
        r.pid        = row[0] ? std::stoull(row[0]) : 0;
        r.body       = row[1] ? std::string(row[1]) : "";
        r.created_at = row[2] ? std::string(row[2]) : "";
        r.aid        = row[3] ? std::stoull(row[3]) : 0;
        r.aname      = row[4] ? std::string(row[4]) : "";
        r.aavatar    = row[5] ? std::string(row[5]) : "";
        rows.push_back(std::move(r));
    }
    mysql_free_result(res);

    std::unordered_map<uint64_t, nlohmann::json> mentions_by_post;
    if (!rows.empty()) {
        std::ostringstream ids;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (i) ids << ",";
            ids << rows[i].pid;
        }
        std::string mq =
            "SELECT m.post_id, m.mention_kind, m.mentioned_user_id, m.mentioned_clip_id, u.username AS mun "
            "FROM profile_wall_mentions m LEFT JOIN users u ON u.id=m.mentioned_user_id WHERE m.post_id IN ("
            + ids.str() + ")";
        if (mysql_query(conn.raw(), mq.c_str()) == 0) {
            MYSQL_RES* mr = mysql_store_result(conn.raw());
            if (mr) {
                while (MYSQL_ROW r2 = mysql_fetch_row(mr)) {
                    uint64_t pid = r2[0] ? std::stoull(r2[0]) : 0;
                    nlohmann::json m;
                    m["kind"] = r2[1] ? std::string(r2[1]) : "";
                    if (r2[2]) m["user_id"] = std::stoull(r2[2]);
                    if (r2[3]) m["clip_id"] = std::stoull(r2[3]);
                    if (r2[4] && r2[4][0]) m["username"] = std::string(r2[4]);
                    if (mentions_by_post.find(pid) == mentions_by_post.end())
                        mentions_by_post[pid] = nlohmann::json::array();
                    mentions_by_post[pid].push_back(std::move(m));
                }
                mysql_free_result(mr);
            }
        }
    }

    for (const auto& r : rows) {
        nlohmann::json mentions = nlohmann::json::array();
        auto it = mentions_by_post.find(r.pid);
        if (it != mentions_by_post.end()) mentions = it->second;
        out.push_back(nlohmann::json{
            {"id", r.pid},
            {"body", r.body},
            {"created_at", r.created_at},
            {"author",
             {{"id", r.aid}, {"username", r.aname}, {"avatar_url", r.aavatar}}},
            {"mentions", std::move(mentions)},
        });
    }
    return out;
}

std::vector<uint64_t> UserRepository::wall_post_mentioned_user_ids(uint64_t post_id) {
    std::vector<uint64_t> out;
    if (post_id == 0) return out;
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT mentioned_user_id FROM profile_wall_mentions WHERE post_id=" + std::to_string(post_id)
        + " AND mention_kind='user' AND mentioned_user_id IS NOT NULL";
    if (mysql_query(conn.raw(), sql.c_str())) return out;
    MYSQL_RES* res = mysql_store_result(conn.raw());
    if (!res) return out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        if (row[0]) out.push_back(std::stoull(row[0]));
    }
    mysql_free_result(res);
    return out;
}

std::optional<uint64_t> UserRepository::create_wall_post(uint64_t author_id, uint64_t profile_owner_id,
                                                         const std::string& body) {
    if (author_id == 0 || profile_owner_id == 0 || author_id != profile_owner_id) return std::nullopt;
    if (body.empty() || body.size() > 4000) return std::nullopt;
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string& s) { return database::escape(conn.raw(), s); };
    std::string sql = "INSERT INTO profile_wall_posts (profile_owner_id, author_id, body) VALUES ("
        + std::to_string(profile_owner_id) + "," + std::to_string(author_id) + ",'" + esc(body) + "')";
    if (!mysql_exec_ok(conn.raw(), sql)) return std::nullopt;
    uint64_t post_id = static_cast<uint64_t>(mysql_insert_id(conn.raw()));
    if (post_id == 0) return std::nullopt;

    auto is_user_char = [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
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
            while (k < body.size() && body[k] >= '0' && body[k] <= '9') ++k;
            if (k > j) {
                uint64_t cid = std::stoull(body.substr(j, k - j));
                std::string ck = "SELECT id FROM clips WHERE id=" + std::to_string(cid) + " LIMIT 1";
                if (mysql_query(conn.raw(), ck.c_str()) == 0) {
                    MYSQL_RES* rs = mysql_store_result(conn.raw());
                    bool ok = rs && mysql_fetch_row(rs);
                    if (rs) mysql_free_result(rs);
                    if (ok) {
                        std::string ins =
                            "INSERT INTO profile_wall_mentions (post_id, mention_kind, mentioned_user_id, mentioned_clip_id) VALUES ("
                            + std::to_string(post_id) + ",'clip',NULL," + std::to_string(cid) + ")";
                        mysql_query(conn.raw(), ins.c_str());
                    }
                }
            }
            i = k;
            continue;
        }
        size_t start = j;
        while (j < body.size() && is_user_char(body[j])) ++j;
        if (j - start >= 2 && j - start <= 32) {
            std::string uname = body.substr(start, j - start);
            std::string qu = "SELECT id FROM users WHERE account_blocked=0 AND username='" + esc(uname) + "' LIMIT 1";
            if (mysql_query(conn.raw(), qu.c_str()) == 0) {
                MYSQL_RES* rs = mysql_store_result(conn.raw());
                MYSQL_ROW rw = rs ? mysql_fetch_row(rs) : nullptr;
                uint64_t uid = 0;
                if (rw && rw[0]) uid = std::stoull(rw[0]);
                if (rs) mysql_free_result(rs);
                if (uid > 0 && uid != author_id) {
                    std::string ins =
                        "INSERT INTO profile_wall_mentions (post_id, mention_kind, mentioned_user_id, mentioned_clip_id) VALUES ("
                        + std::to_string(post_id) + ",'user'," + std::to_string(uid) + ",NULL)";
                    mysql_query(conn.raw(), ins.c_str());
                }
            }
        }
        i = j;
    }
    return post_id;
}

} // namespace clipstream::repositories
