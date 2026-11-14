#include "repositories/ProfileRepository.hpp"
#include "database/SqlEscape.hpp"
#include <algorithm>
#include <charconv>
#include <set>

namespace clipstream::repositories {
using json = nlohmann::json;
namespace {
std::string text(MYSQL_ROW row, unsigned index) { return row[index] ? row[index] : ""; }
uint64_t integer(const std::string &value) {
    uint64_t number{};
    auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (ec != std::errc{} || end != value.data() + value.size())
        return 0;
    return number;
}
std::string path_component(const std::string &value) {
    std::string out;
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
std::string quote(MYSQL *conn, const std::string &value) { return "'" + database::escape(conn, value) + "'"; }
uint64_t scalar(MYSQL *conn, const std::string &sql) {
    database::query(conn, sql.c_str());
    database::Result rows(mysql_store_result(conn));
    if (!rows)
        throw database::Error(mysql_errno(conn));
    auto row = mysql_fetch_row(rows);
    return row ? integer(text(row, 0)) : 0;
}
} // namespace
std::vector<ProfileEntity> ProfileRepository::select(const std::string &suffix) {
    database::ScopedConnection conn(pool_);
    const auto sql = "SELECT "
                     "p.profile_key,COALESCE(p.twitch_id,''),COALESCE(p.local_user_id,0),p.last_login,p."
                     "display_name,p.avatar_url,COALESCE(u.username,''),TIMESTAMPDIFF(SECOND,p.refreshed_at,"
                     "UTC_TIMESTAMP()) FROM profile_entities p LEFT JOIN users u ON u.id=p.local_user_id " +
                     suffix;
    database::query(conn.raw(), sql.c_str());
    database::Result rows(mysql_store_result(conn.raw()));
    if (!rows)
        throw database::Error(mysql_errno(conn.raw()));
    std::vector<ProfileEntity> out;
    while (auto row = mysql_fetch_row(rows))
        out.push_back({text(row, 0), text(row, 1), integer(text(row, 2)), text(row, 3), text(row, 4),
                       text(row, 5), text(row, 6), static_cast<int>(integer(text(row, 7)))});
    return out;
}
std::optional<ProfileEntity> ProfileRepository::find(const std::string &key) {
    if (key.size() > 96 || key.size() < 3 || !std::all_of(key.begin(), key.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || c == 't' || c == 'u' || c == '-';
        }))
        return std::nullopt;
    auto rows = select("WHERE p.profile_key='" + key +
                       "' AND (p.local_user_id IS NOT NULL OR p.twitch_id IS NOT NULL) LIMIT 1");
    if (rows.empty())
        return std::nullopt;
    return rows[0];
}
ProfileEntity ProfileRepository::ensure_twitch(const std::string &id, const std::string &login,
                                               const std::string &display, const std::string &avatar) {
    if (id.empty() || id.size() > 64 ||
        !std::all_of(id.begin(), id.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
        throw std::invalid_argument("Invalid Twitch ID");
    const auto key = "t-" + id;
    {
        database::ScopedConnection conn(pool_);
        database::Transaction tx(conn.raw());
        // Lock the owner before either identity row, so concurrent alias resolutions merge consistently.
        database::query(
            conn.raw(),
            ("SELECT id FROM users WHERE twitch_id=" + quote(conn.raw(), id) + " FOR UPDATE").c_str());
        database::Result owners(mysql_store_result(conn.raw()));
        if (!owners)
            throw database::Error(mysql_errno(conn.raw()));
        auto owner_row = mysql_fetch_row(owners);
        const auto uid = owner_row ? integer(text(owner_row, 0)) : 0;
        owners.reset();
        database::query(
            conn.raw(),
            ("INSERT INTO profile_entities(profile_key,twitch_id,last_login,display_name,avatar_url) "
             "VALUES(" +
             quote(conn.raw(), key) + "," + quote(conn.raw(), id) + "," + quote(conn.raw(), login) + "," +
             quote(conn.raw(), display) + "," + quote(conn.raw(), avatar) +
             ") ON DUPLICATE KEY UPDATE "
             "last_login=IF(VALUES(last_login)='',last_login,VALUES(last_login)),display_name=IF(VALUES("
             "display_name)='',display_name,VALUES(display_name)),avatar_url=IF(VALUES(avatar_url)='',avatar_"
             "url,VALUES(avatar_url)),refreshed_at=IF(VALUES(last_login)='',refreshed_at,UTC_TIMESTAMP())")
                .c_str());
        if (uid) {
            database::query(conn.raw(), ("SELECT profile_key FROM profile_entities WHERE local_user_id=" +
                                         std::to_string(uid) + " FOR UPDATE")
                                            .c_str());
            database::Result old_rows(mysql_store_result(conn.raw()));
            if (!old_rows)
                throw database::Error(mysql_errno(conn.raw()));
            auto row = mysql_fetch_row(old_rows);
            const auto old = row ? text(row, 0) : std::string{};
            old_rows.reset();
            if (!old.empty() && old != key) {
                database::query(
                    conn.raw(),
                    ("INSERT IGNORE INTO profile_follows(follower_user_id,profile_key,created_at) SELECT "
                     "follower_user_id," +
                     quote(conn.raw(), key) +
                     ",created_at FROM profile_follows WHERE profile_key=" + quote(conn.raw(), old))
                        .c_str());
                database::query(
                    conn.raw(),
                    ("DELETE FROM profile_entities WHERE profile_key=" + quote(conn.raw(), old)).c_str());
            }
            database::query(conn.raw(), ("UPDATE profile_entities SET local_user_id=" + std::to_string(uid) +
                                         " WHERE profile_key=" + quote(conn.raw(), key))
                                            .c_str());
            database::query(conn.raw(), ("DELETE FROM profile_follows WHERE follower_user_id=" +
                                         std::to_string(uid) + " AND profile_key=" + quote(conn.raw(), key))
                                            .c_str());
        }
        tx.commit();
    }
    return *find(key);
}
ProfileEntity ProfileRepository::ensure_user(const models::User &user) {
    if (!user.twitch_id.empty())
        return ensure_twitch(user.twitch_id, "", "", "");
    const auto key = "u-" + std::to_string(user.id);
    {
        database::ScopedConnection conn(pool_);
        database::query(
            conn.raw(),
            ("INSERT INTO profile_entities(profile_key,local_user_id,display_name,avatar_url) VALUES(" +
             quote(conn.raw(), key) + "," + std::to_string(user.id) + "," + quote(conn.raw(), user.username) +
             "," + quote(conn.raw(), user.avatar_url) +
             ") ON DUPLICATE KEY UPDATE display_name=VALUES(display_name),avatar_url=VALUES(avatar_url)")
                .c_str());
    }
    return *find(key);
}
bool ProfileRepository::follow(uint64_t viewer, const std::string &key) {
    database::ScopedConnection conn(pool_);
    database::query(conn.raw(),
                    ("INSERT IGNORE INTO profile_follows(follower_user_id,profile_key) SELECT " +
                     std::to_string(viewer) +
                     ",profile_key FROM profile_entities WHERE profile_key=" + quote(conn.raw(), key) +
                     " AND (local_user_id IS NULL OR local_user_id<>" + std::to_string(viewer) + ")")
                        .c_str());
    return mysql_affected_rows(conn.raw()) > 0;
}
void ProfileRepository::unfollow(uint64_t viewer, const std::string &key) {
    database::ScopedConnection conn(pool_);
    database::query(conn.raw(), ("DELETE FROM profile_follows WHERE follower_user_id=" +
                                 std::to_string(viewer) + " AND profile_key=" + quote(conn.raw(), key))
                                    .c_str());
}
bool ProfileRepository::is_following(uint64_t viewer, const std::string &key) {
    if (!viewer)
        return false;
    database::ScopedConnection conn(pool_);
    return scalar(conn.raw(), "SELECT COUNT(*) FROM profile_follows WHERE follower_user_id=" +
                                  std::to_string(viewer) + " AND profile_key=" + quote(conn.raw(), key)) > 0;
}
std::unordered_set<std::string>
ProfileRepository::following_among(uint64_t viewer, const std::vector<ProfileEntity> &entities) {
    std::unordered_set<std::string> out;
    if (!viewer || entities.empty())
        return out;
    database::ScopedConnection conn(pool_);
    std::string keys;
    for (const auto &entity : entities) {
        if (!keys.empty())
            keys += ",";
        keys += quote(conn.raw(), entity.key);
    }
    database::query(conn.raw(), ("SELECT profile_key FROM profile_follows WHERE follower_user_id=" +
                                 std::to_string(viewer) + " AND profile_key IN (" + keys + ")")
                                    .c_str());
    database::Result rows(mysql_store_result(conn.raw()));
    if (!rows)
        throw database::Error(mysql_errno(conn.raw()));
    while (auto row = mysql_fetch_row(rows))
        out.insert(text(row, 0));
    return out;
}
std::vector<ProfileEntity> ProfileRepository::following(uint64_t user, uint32_t limit, uint32_t offset) {
    return select("JOIN profile_follows f ON f.profile_key=p.profile_key WHERE f.follower_user_id=" +
                  std::to_string(user) +
                  " AND (p.local_user_id IS NOT NULL OR p.twitch_id IS NOT NULL) ORDER BY f.created_at "
                  "DESC,p.profile_key LIMIT " +
                  std::to_string(std::clamp(limit, 1u, 101u)) + " OFFSET " + std::to_string(offset));
}
std::vector<ProfileEntity> ProfileRepository::followers(const std::string &key, uint32_t limit,
                                                        uint32_t offset) {
    if (!find(key))
        return {};
    return select("JOIN profile_follows f ON f.follower_user_id=p.local_user_id WHERE f.profile_key='" + key +
                  "' ORDER BY f.created_at DESC,p.profile_key LIMIT " +
                  std::to_string(std::clamp(limit, 1u, 101u)) + " OFFSET " + std::to_string(offset));
}
json ProfileRepository::stats(const ProfileEntity &p) {
    database::ScopedConnection conn(pool_);
    json out;
    out["followers"] = scalar(conn.raw(), "SELECT COUNT(*) FROM profile_follows WHERE profile_key=" +
                                              quote(conn.raw(), p.key));
    out["following"] =
        p.user_id ? scalar(conn.raw(), "SELECT COUNT(*) FROM profile_follows WHERE follower_user_id=" +
                                           std::to_string(p.user_id))
                  : 0;
    out["friends"] = p.user_id ? scalar(conn.raw(), "SELECT COUNT(*) FROM friendships WHERE user_id=" +
                                                        std::to_string(p.user_id))
                               : 0;
    std::string filter = p.twitch_id.empty() ? "author_id=" + std::to_string(p.user_id)
                                             : "broadcaster_id=" + quote(conn.raw(), p.twitch_id);
    if (!p.twitch_id.empty())
        filter += " OR clip_creator_twitch_id=" + quote(conn.raw(), p.twitch_id);
    if (p.user_id && !p.twitch_id.empty())
        filter += " OR author_id=" + std::to_string(p.user_id);
    out["clips"] = scalar(
        conn.raw(), "SELECT COUNT(*) FROM clips WHERE unavailable_at IS NULL AND moderation_hidden=0 AND (" +
                        filter + ")");
    return out;
}
json ProfileRepository::settings(uint64_t user) {
    json out = {{"display_name", ""},
                {"bio", ""},
                {"website", ""},
                {"profile_visibility", "public"},
                {"followers_visibility", "public"},
                {"following_visibility", "private"},
                {"wall_policy", "friends"}};
    database::ScopedConnection conn(pool_);
    database::query(conn.raw(), ("SELECT "
                                 "display_name,bio,website,profile_visibility,followers_visibility,following_"
                                 "visibility,wall_policy FROM user_profile_settings WHERE user_id=" +
                                 std::to_string(user))
                                    .c_str());
    database::Result rows(mysql_store_result(conn.raw()));
    if (!rows)
        throw database::Error(mysql_errno(conn.raw()));
    if (auto row = mysql_fetch_row(rows)) {
        const char *keys[] = {
            "display_name",         "bio",        "website", "profile_visibility", "followers_visibility",
            "following_visibility", "wall_policy"};
        for (unsigned i = 0; i < 7; ++i)
            out[keys[i]] = text(row, i);
    }
    return out;
}
void ProfileRepository::save_settings(uint64_t user, const json &patch) {
    if (!patch.is_object())
        throw std::invalid_argument("Profile settings must be an object");
    const std::set<std::string> fields = {
        "display_name",         "bio",        "website", "profile_visibility", "followers_visibility",
        "following_visibility", "wall_policy"};
    std::string columns, values, updates;
    database::ScopedConnection conn(pool_);
    for (const auto &[key, value] : patch.items()) {
        if (!fields.contains(key) || !value.is_string())
            throw std::invalid_argument("Invalid profile setting");
        const auto text = value.get<std::string>();
        if (key.ends_with("visibility") && text != "public" && text != "friends" && text != "private")
            throw std::invalid_argument("Invalid profile visibility");
        if (key == "wall_policy" && text != "everyone" && text != "friends" && text != "none")
            throw std::invalid_argument("Invalid wall policy");
        const auto maximum = key == "bio"            ? 2000u
                             : key == "display_name" ? 128u
                             : key == "website"      ? 512u
                                                     : 32u;
        if (text.size() > maximum)
            throw std::invalid_argument("Profile text is too long");
        if (key == "website" && !text.empty()) {
            const auto prefix = text.starts_with("https://") ? 8u : text.starts_with("http://") ? 7u : 0u;
            if (!prefix || text.size() <= prefix ||
                std::any_of(text.begin(), text.end(), [](unsigned char c) { return c <= 32 || c == 127; }))
                throw std::invalid_argument("Website must be a valid HTTP or HTTPS URL");
        }
        columns += "," + key;
        values += "," + quote(conn.raw(), text);
        if (!updates.empty())
            updates += ",";
        updates += key + "=VALUES(" + key + ")";
    }
    if (updates.empty())
        return;
    database::query(conn.raw(), ("INSERT INTO user_profile_settings(user_id" + columns + ") VALUES(" +
                                 std::to_string(user) + values + ") ON DUPLICATE KEY UPDATE " + updates)
                                    .c_str());
}
bool ProfileRepository::may_view(uint64_t owner, uint64_t viewer, const std::string &field) {
    if (!owner)
        return true;
    if (owner == viewer)
        return true;
    if (field != "profile_visibility" && field != "followers_visibility" && field != "following_visibility")
        throw std::invalid_argument("Invalid privacy field");
    auto prefs = settings(owner);
    const auto visibility = prefs[field].get<std::string>();
    if (visibility == "public")
        return true;
    if (visibility == "private" || !viewer)
        return false;
    database::ScopedConnection conn(pool_);
    return scalar(conn.raw(), "SELECT COUNT(*) FROM friendships WHERE user_id=" + std::to_string(viewer) +
                                  " AND friend_id=" + std::to_string(owner)) > 0;
}
bool ProfileRepository::may_post_wall(uint64_t owner, uint64_t viewer) {
    if (!owner || !viewer || !may_view(owner, viewer))
        return false;
    if (owner == viewer)
        return true;
    const auto policy = settings(owner).value("wall_policy", "friends");
    if (policy == "everyone")
        return true;
    if (policy == "none")
        return false;
    database::ScopedConnection conn(pool_);
    return scalar(conn.raw(), "SELECT COUNT(*) FROM friendships WHERE user_id=" + std::to_string(viewer) +
                                  " AND friend_id=" + std::to_string(owner)) > 0;
}
ProfileRepository::WallMutation ProfileRepository::mutate_wall(uint64_t post, uint64_t viewer, bool staff,
                                                               bool remove, const std::string &body) {
    if (!viewer)
        return WallMutation::Forbidden;
    if (!remove && (body.empty() || body.size() > 4000))
        throw std::invalid_argument("Post body must contain 1 to 4000 bytes");
    database::ScopedConnection conn(pool_);
    database::Transaction tx(conn.raw());
    database::query(conn.raw(), ("SELECT author_id,profile_owner_id FROM profile_wall_posts WHERE id=" +
                                 std::to_string(post) + " FOR UPDATE")
                                    .c_str());
    database::Result rows(mysql_store_result(conn.raw()));
    if (!rows)
        throw database::Error(mysql_errno(conn.raw()));
    auto row = mysql_fetch_row(rows);
    if (!row)
        return WallMutation::NotFound;
    const auto author = integer(text(row, 0)), owner = integer(text(row, 1));
    rows.reset();
    if (remove ? (viewer != author && viewer != owner && !staff) : viewer != author)
        return WallMutation::Forbidden;
    // Edits deliberately do not emit new mention notifications. Remove old derived references atomically.
    database::query(conn.raw(),
                    ("DELETE FROM notifications WHERE wall_post_id=" + std::to_string(post)).c_str());
    if (remove)
        database::query(conn.raw(),
                        ("DELETE FROM profile_wall_posts WHERE id=" + std::to_string(post)).c_str());
    else {
        database::query(conn.raw(),
                        ("DELETE FROM profile_wall_mentions WHERE post_id=" + std::to_string(post)).c_str());
        database::query(conn.raw(), ("UPDATE profile_wall_posts SET body=" + quote(conn.raw(), body) +
                                     " WHERE id=" + std::to_string(post))
                                        .c_str());
    }
    tx.commit();
    return WallMutation::Applied;
}
json ProfileRepository::card(const ProfileEntity &p) {
    return {{"key", p.key},
            {"canonical_path",
             p.twitch_id.empty() ? "/p/user/" + path_component(p.username) : "/p/twitch/" + p.twitch_id},
            {"local_user", p.user_id ? json({{"id", p.user_id}, {"username", p.username}}) : json(nullptr)},
            {"twitch", p.twitch_id.empty() ? json(nullptr)
                                           : json({{"id", p.twitch_id},
                                                   {"login", p.login},
                                                   {"display_name", p.display_name},
                                                   {"profile_image_url", p.avatar_url}})},
            {"display_name", p.display_name.empty() ? p.username : p.display_name},
            {"avatar_url", p.avatar_url}};
}
} // namespace clipstream::repositories
