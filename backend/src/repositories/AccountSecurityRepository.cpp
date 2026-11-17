#include "repositories/AccountSecurityRepository.hpp"
#include "database/SqlEscape.hpp"
#include "utils/Errors.hpp"
#include "utils/Network.hpp"
#include <algorithm>
#include <stdexcept>
namespace clipstream::repositories {
namespace {
std::string number(uint64_t value) { return std::to_string(value); }
std::string quoted(MYSQL *c, const std::string &value) { return "'" + database::escape(c, value) + "'"; }
std::string staff_role(MYSQL *c, uint64_t id) {
    auto sql = "SELECT `role` FROM users WHERE id=" + number(id) +
               " AND account_blocked=0 AND (account_suspended_until IS NULL OR "
               "account_suspended_until<=UTC_TIMESTAMP())";
    database::query(c, sql.c_str());
    database::Result result(mysql_store_result(c));
    auto row = mysql_fetch_row(result);
    if (!row || !row[0] || (std::string(row[0]) != "admin" && std::string(row[0]) != "moderator"))
        throw utils::PermissionDenied("Active staff privileges required");
    return row[0];
}
void log(MYSQL *c, uint64_t actor, const std::string &type, const std::string &target,
         const std::string &action, const std::string &reason, const nlohmann::json &details = {}) {
    if (reason.size() > 512)
        throw std::invalid_argument("Reason must not exceed 512 bytes");
    auto sql = "INSERT INTO moderation_audit(actor_id,target_type,target_id,action,reason,details) VALUES(" +
               number(actor) + "," + quoted(c, type) + "," + quoted(c, target) + "," + quoted(c, action) +
               "," + quoted(c, reason) + "," + quoted(c, details.dump()) + ")";
    database::query(c, sql.c_str());
}
void lock_admins(MYSQL *c) {
    database::query(c, "SELECT id FROM users WHERE `role`='admin' ORDER BY id FOR UPDATE");
    database::Result result(mysql_store_result(c));
}
} // namespace
bool AccountSecurityRepository::bootstrap_pending() {
    database::ScopedConnection conn(pool_);
    database::query(conn.raw(),
                    "SELECT 1 FROM admin_bootstrap WHERE id=1 AND eligible=1 AND completed_by IS NULL");
    database::Result result(mysql_store_result(conn.raw()));
    return mysql_fetch_row(result) != nullptr;
}
bool AccountSecurityRepository::bootstrap_admin(uint64_t id, const std::string &expected) {
    return database::retry_transaction([&]() -> bool {
        if (expected.empty())
            return false;
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        database::query(c,
                        "SELECT eligible,twitch_id,completed_by FROM admin_bootstrap WHERE id=1 FOR UPDATE");
        database::Result result(mysql_store_result(c));
        auto row = mysql_fetch_row(result);
        if (!row || std::string(row[0]) != "1" || row[2])
            return false;
        std::string candidate = row[1] ? row[1] : "";
        result.reset();
        if (candidate.empty()) {
            auto set = "UPDATE admin_bootstrap SET twitch_id=" + quoted(c, expected) + " WHERE id=1";
            database::query(c, set.c_str());
            candidate = expected;
        }
        auto sql = "SELECT twitch_id FROM users WHERE id=" + number(id) + " FOR UPDATE";
        database::query(c, sql.c_str());
        database::Result user(mysql_store_result(c));
        auto identity = mysql_fetch_row(user);
        if (!identity || !identity[0] || candidate != identity[0]) {
            tx.commit();
            return false;
        }
        user.reset();
        database::query(c, "SELECT id FROM users WHERE `role`='admin' LIMIT 1");
        database::Result existing(mysql_store_result(c));
        if (mysql_fetch_row(existing)) {
            tx.commit();
            return false;
        }
        existing.reset();
        auto grant = "UPDATE users SET `role`='admin',auth_version=auth_version+1 WHERE id=" + number(id);
        database::query(c, grant.c_str());
        auto complete = "UPDATE admin_bootstrap SET completed_by=" + number(id) +
                        ",completed_at=UTC_TIMESTAMP() WHERE id=1";
        database::query(c, complete.c_str());
        log(c, id, "user", number(id), "bootstrap_admin", "Verified Twitch identity deexsd",
            {{"twitch_id", candidate}});
        tx.commit();
        return true;
    });
}
bool AccountSecurityRepository::is_ip_banned(const std::string &value) {
    auto ip = utils::normalize_ip(value);
    database::ScopedConnection conn(pool_);
    auto q = "SELECT 1 FROM ip_bans WHERE ip=" + quoted(conn.raw(), ip) +
             " AND (expires_at IS NULL OR expires_at>UTC_TIMESTAMP())";
    database::query(conn.raw(), q.c_str());
    database::Result result(mysql_store_result(conn.raw()));
    return mysql_fetch_row(result) != nullptr;
}
void AccountSecurityRepository::restrict_user(uint64_t actor, uint64_t target, const std::string &action,
                                              unsigned hours, const std::string &reason) {
    return database::retry_transaction([&]() -> void {
        if (reason.size() > 512 || reason.find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::invalid_argument("A non-empty reason of at most 512 bytes is required");
        if (hours > 8760)
            throw std::invalid_argument("Duration must not exceed 8760 hours");
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        lock_admins(c);
        auto actor_role = staff_role(c, actor);
        auto q = "SELECT `role`,account_blocked,(account_suspended_until IS NOT NULL AND "
                 "account_suspended_until>UTC_TIMESTAMP()) FROM users WHERE id=" +
                 number(target) + " FOR UPDATE";
        database::query(c, q.c_str());
        database::Result result(mysql_store_result(c));
        auto row = mysql_fetch_row(result);
        if (!row)
            throw utils::NotFound("Target user not found");
        std::string role = row[0];
        bool active = std::string(row[1]) == "0" && std::string(row[2]) == "0";
        result.reset();
        if (actor_role != "admin" && (role == "admin" || role == "moderator"))
            throw utils::PermissionDenied("Cannot moderate an equal or higher staff role");
        bool restricting = action == "block_account" || action == "suspend_account" || action == "chat_ban";
        if (restricting && actor == target)
            throw std::invalid_argument("Cannot restrict your own account");
        if (restricting && role == "admin" && active) {
            database::query(c,
                            "SELECT COUNT(*) FROM users WHERE `role`='admin' AND account_blocked=0 AND "
                            "(account_suspended_until IS NULL OR account_suspended_until<=UTC_TIMESTAMP())");
            database::Result count(mysql_store_result(c));
            auto r = mysql_fetch_row(count);
            if (!r || std::stoul(r[0]) <= 1)
                throw std::invalid_argument("Cannot restrict the last active administrator");
        }
        std::string set;
        if (action == "block_account")
            set = "account_blocked=1,account_suspended_until=NULL,auth_version=auth_version+1";
        else if (action == "suspend_account") {
            if (hours == 0)
                throw std::invalid_argument("Suspension requires positive hours");
            set = "account_blocked=0,account_suspended_until=UTC_TIMESTAMP()+INTERVAL " + number(hours) +
                  " HOUR,auth_version=auth_version+1";
        } else if (action == "unblock_account")
            set = "account_blocked=0,account_suspended_until=NULL,auth_version=auth_version+1";
        else if (action == "chat_ban")
            set = "chat_banned_permanently=" + std::string(hours ? "0" : "1") + ",chat_banned_until=" +
                  (hours ? "UTC_TIMESTAMP()+INTERVAL " + number(hours) + " HOUR" : std::string("NULL")) +
                  ",chat_ban_reason=" + quoted(c, reason) + ",chat_banned_by=" + number(actor);
        else if (action == "lift_chat")
            set = "chat_banned_permanently=0,chat_banned_until=NULL,chat_ban_reason=NULL,chat_banned_by=NULL";
        else
            throw std::invalid_argument("Unknown moderation action");
        if (action == "block_account" || action == "suspend_account" || action == "unblock_account")
            set += ",account_ban_reason=" + quoted(c, reason) + ",account_banned_by=" + number(actor);
        auto update = "UPDATE users SET " + set + " WHERE id=" + number(target);
        database::query(c, update.c_str());
        log(c, actor, "user", number(target), action, reason, {{"hours", hours}});
        tx.commit();
    });
}
nlohmann::json AccountSecurityRepository::find_users(uint64_t actor, const std::string &search,
                                                     const std::string &status, uint32_t limit,
                                                     uint32_t offset) {
    database::ScopedConnection conn(pool_);
    auto c = conn.raw();
    const bool admin = staff_role(c, actor) == "admin";
    if (search.size() > 256)
        throw std::invalid_argument("Search must not exceed 256 bytes");
    std::string where = "1=1";
    if (!search.empty()) {
        std::string literal;
        for (char ch : search) {
            if (ch == '!' || ch == '%' || ch == '_')
                literal += '!';
            literal += ch;
        }
        const auto pattern = quoted(c, "%" + literal + "%") + " ESCAPE '!'";
        where += " AND (username LIKE " + pattern;
        if (admin)
            where += " OR email LIKE " + pattern;
        where += ")";
    }
    const std::string suspended = "(account_suspended_until>UTC_TIMESTAMP())";
    const std::string chat = "(chat_banned_permanently=1 OR chat_banned_until>UTC_TIMESTAMP())";
    if (status == "blocked")
        where += " AND account_blocked=1";
    else if (status == "suspended")
        where += " AND account_blocked=0 AND " + suspended;
    else if (status == "chat")
        where += " AND " + chat;
    else if (status == "active")
        where += " AND account_blocked=0 AND (account_suspended_until IS NULL OR "
                 "account_suspended_until<=UTC_TIMESTAMP())";
    else if (!status.empty())
        throw std::invalid_argument("Unknown account status");
    auto count = "SELECT COUNT(*) FROM users WHERE " + where;
    database::query(c, count.c_str());
    database::Result counted(mysql_store_result(c));
    const auto total = std::stoull(mysql_fetch_row(counted)[0]);
    counted.reset();
    limit = std::clamp(limit, 1u, 100u);
    auto sql = "SELECT id,username,avatar_url,`role`,created_at,account_blocked,"
               "account_suspended_until,chat_banned_permanently,chat_banned_until," +
               std::string(admin ? "email" : "NULL") + " FROM users WHERE " + where +
               " ORDER BY id DESC LIMIT " + number(limit) + " OFFSET " + number(offset);
    database::query(c, sql.c_str());
    database::Result result(mysql_store_result(c));
    auto rows = nlohmann::json::array();
    while (auto r = mysql_fetch_row(result)) {
        rows.push_back({{"id", std::stoull(r[0])},
                        {"username", r[1]},
                        {"avatar_url", r[2] ? r[2] : ""},
                        {"role", r[3]},
                        {"created_at", r[4]},
                        {"account_blocked", std::string(r[5]) == "1"},
                        {"account_suspended_until", r[6] ? nlohmann::json(r[6]) : nlohmann::json(nullptr)},
                        {"chat_banned_permanently", std::string(r[7]) == "1"},
                        {"chat_banned_until", r[8] ? nlohmann::json(r[8]) : nlohmann::json(nullptr)}});
        if (admin)
            rows.back()["email"] = r[9] ? r[9] : "";
    }
    return {{"users", rows}, {"total", total}, {"has_more", offset + rows.size() < total}};
}
nlohmann::json AccountSecurityRepository::user_state(uint64_t actor, uint64_t target) {
    database::ScopedConnection conn(pool_);
    auto c = conn.raw();
    staff_role(c, actor);
    auto q = "SELECT "
             "id,username,`role`,account_blocked,account_suspended_until,account_ban_reason,chat_banned_"
             "until,chat_ban_reason,chat_banned_permanently FROM users WHERE id=" +
             number(target);
    database::query(c, q.c_str());
    database::Result result(mysql_store_result(c));
    auto row = mysql_fetch_row(result);
    if (!row)
        throw utils::NotFound("User not found");
    return {{"id", std::stoull(row[0])},
            {"username", row[1]},
            {"role", row[2]},
            {"account_blocked", std::string(row[3]) == "1"},
            {"account_suspended_until", row[4] ? nlohmann::json(row[4]) : nlohmann::json(nullptr)},
            {"account_ban_reason", row[5] ? row[5] : ""},
            {"chat_banned_until", row[6] ? nlohmann::json(row[6]) : nlohmann::json(nullptr)},
            {"chat_ban_reason", row[7] ? row[7] : ""},
            {"chat_banned_permanently", row[8] && std::string(row[8]) == "1"}};
}
nlohmann::json AccountSecurityRepository::ip_bans(uint64_t actor, uint32_t limit, uint32_t offset) {
    database::ScopedConnection conn(pool_);
    auto c = conn.raw();
    staff_role(c, actor);
    auto q = "SELECT id,ip,reason,actor_id,expires_at,created_at FROM ip_bans ORDER BY id DESC LIMIT " +
             number(std::min(limit, 100u)) + " OFFSET " + number(offset);
    database::query(c, q.c_str());
    database::Result result(mysql_store_result(c));
    auto out = nlohmann::json::array();
    while (auto r = mysql_fetch_row(result))
        out.push_back({{"id", std::stoull(r[0])},
                       {"ip", r[1]},
                       {"reason", r[2]},
                       {"actor_id", std::stoull(r[3])},
                       {"expires_at", r[4] ? nlohmann::json(r[4]) : nlohmann::json(nullptr)},
                       {"created_at", r[5]}});
    return out;
}
uint64_t AccountSecurityRepository::ban_ip(uint64_t actor, const std::string &value, unsigned hours,
                                           const std::string &reason, const std::string &actor_ip) {
    return database::retry_transaction([&]() -> uint64_t {
        if (reason.size() > 512 || reason.find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::invalid_argument("A non-empty reason of at most 512 bytes is required");
        auto ip = utils::normalize_ip(value);
        if (ip == utils::normalize_ip(actor_ip))
            throw std::invalid_argument("Cannot ban your own IP address");
        if (hours > 8760)
            throw std::invalid_argument("Duration exceeds 8760 hours");
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        staff_role(c, actor);
        auto until = hours ? "UTC_TIMESTAMP()+INTERVAL " + number(hours) + " HOUR" : std::string("NULL");
        auto q = "INSERT INTO ip_bans(ip,reason,actor_id,expires_at) VALUES(" + quoted(c, ip) + "," +
                 quoted(c, reason) + "," + number(actor) + "," + until +
                 ") ON DUPLICATE KEY UPDATE "
                 "id=LAST_INSERT_ID(id),reason=VALUES(reason),actor_id=VALUES(actor_id),expires_at=VALUES("
                 "expires_at)";
        database::query(c, q.c_str());
        auto id = mysql_insert_id(c);
        log(c, actor, "ip", ip, "ban_ip", reason, {{"hours", hours}});
        tx.commit();
        return id;
    });
}
void AccountSecurityRepository::unban_ip(uint64_t actor, uint64_t id) {
    return database::retry_transaction([&]() -> void {
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        staff_role(c, actor);
        auto q = "DELETE FROM ip_bans WHERE id=" + number(id);
        database::query(c, q.c_str());
        if (!mysql_affected_rows(c))
            throw utils::NotFound("IP ban not found");
        log(c, actor, "ip", number(id), "unban_ip", "");
        tx.commit();
    });
}
nlohmann::json AccountSecurityRepository::audit(uint64_t actor, uint32_t limit, uint32_t offset) {
    database::ScopedConnection conn(pool_);
    auto c = conn.raw();
    staff_role(c, actor);
    auto q = "SELECT a.id,a.actor_id,a.target_type,a.target_id,a.action,a.reason,a.details,a.created_at,"
             "actor.username,target.username FROM moderation_audit a "
             "LEFT JOIN users actor ON actor.id=a.actor_id "
             "LEFT JOIN users target ON a.target_type='user' AND target.id=CAST(a.target_id AS UNSIGNED) "
             "ORDER BY a.id DESC LIMIT " +
             number(std::min(limit, 100u)) + " OFFSET " + number(offset);
    database::query(c, q.c_str());
    database::Result result(mysql_store_result(c));
    auto out = nlohmann::json::array();
    while (auto r = mysql_fetch_row(result))
        out.push_back({{"id", std::stoull(r[0])},
                       {"actor_id", std::stoull(r[1])},
                       {"target_type", r[2]},
                       {"target_id", r[3]},
                       {"action", r[4]},
                       {"reason", r[5]},
                       {"details", r[6] ? nlohmann::json::parse(r[6]) : nlohmann::json(nullptr)},
                       {"actor_username", r[8] ? r[8] : ""},
                       {"target_username", r[9] ? r[9] : ""},
                       {"created_at", r[7]}});
    return out;
}
void AccountSecurityRepository::classify_clip(uint64_t actor, uint64_t id, bool mature,
                                              const std::string &reason) {
    return database::retry_transaction([&]() -> void {
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        staff_role(c, actor);
        auto q = "UPDATE clips SET is_mature=" + std::string(mature ? "1" : "0") +
                 ",classification_source='manual' WHERE id=" + number(id);
        database::query(c, q.c_str());
        log(c, actor, "clip", number(id), "classify_clip", reason, {{"is_mature", mature}});
        tx.commit();
    });
}
void AccountSecurityRepository::resolve_report(uint64_t actor, uint64_t id, const std::string &resolution,
                                               const std::string &reason) {
    return database::retry_transaction([&]() -> void {
        if (resolution != "dismissed" && resolution != "actioned")
            throw std::invalid_argument("resolution must be dismissed or actioned");
        if (reason.size() > 512)
            throw std::invalid_argument("Reason exceeds 512 bytes");
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        staff_role(c, actor);
        auto lock = "SELECT id FROM content_reports WHERE id=" + number(id) + " FOR UPDATE";
        database::query(c, lock.c_str());
        database::Result result(mysql_store_result(c));
        if (!mysql_fetch_row(result))
            throw utils::NotFound("Report not found");
        result.reset();
        auto update = "UPDATE content_reports SET status='" +
                      std::string(resolution == "dismissed" ? "dismissed" : "reviewed") +
                      "',resolution=" + quoted(c, resolution) + ",reviewed_by=" + number(actor) +
                      ",reviewed_at=UTC_TIMESTAMP(),resolution_reason=" + quoted(c, reason) +
                      " WHERE id=" + number(id);
        database::query(c, update.c_str());
        log(c, actor, "report", number(id), "resolve_report", reason, {{"resolution", resolution}});
        tx.commit();
    });
}
void AccountSecurityRepository::set_clip_visibility(uint64_t actor, uint64_t id, bool hidden,
                                                    const std::string &reason) {
    return database::retry_transaction([&]() -> void {
        if (reason.size() > 512)
            throw std::invalid_argument("Reason exceeds 512 bytes");
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        staff_role(c, actor);
        auto lock = "SELECT id FROM clips WHERE id=" + number(id) + " FOR UPDATE";
        database::query(c, lock.c_str());
        database::Result result(mysql_store_result(c));
        if (!mysql_fetch_row(result))
            throw utils::NotFound("Clip not found");
        result.reset();
        auto update = "UPDATE clips SET moderation_hidden=" + std::string(hidden ? "1" : "0") +
                      " WHERE id=" + number(id);
        database::query(c, update.c_str());
        log(c, actor, "clip", number(id), hidden ? "hide_clip" : "restore_clip", reason);
        tx.commit();
    });
}
} // namespace clipstream::repositories
