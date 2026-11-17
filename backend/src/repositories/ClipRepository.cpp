#include "repositories/ClipRepository.hpp"
#include "repositories/RecommendationWrites.hpp"
#include "repositories/ClipFeedSql.hpp"
#include "database/SqlEscape.hpp"
#include "utils/TitleFingerprint.hpp"
#include "utils/Utf8.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <deque>
#include <functional>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace clipstream::repositories {
namespace {
using namespace feed_sql;

void append_game_filter(std::ostringstream &sql, const std::vector<uint64_t> &ids) {
    if (ids.empty())
        return;
    sql << " AND c.game_id IN (";
    append_in_numeric(sql, ids);
    sql << ") ";
}

std::vector<std::string> split_terms(const std::string &query) {
    std::istringstream ss(query);
    std::vector<std::string> terms;
    std::string token;
    while (ss >> token) {
        token.erase(std::remove_if(token.begin(), token.end(),
                                   [](unsigned char ch) { return std::iscntrl(ch) != 0; }),
                    token.end());
        if (token.size() >= 2)
            terms.push_back(token);
    }
    return terms;
}

const char *k_clip_platform_aggr_join = "LEFT JOIN (SELECT clip_id, COUNT(*) AS plat_pv FROM view_history "
                                        "GROUP BY clip_id) _plat_v ON _plat_v.clip_id=c.id "
                                        "LEFT JOIN (SELECT clip_id, COUNT(*) AS plat_pc FROM comments GROUP "
                                        "BY clip_id) _plat_c ON _plat_c.clip_id=c.id "
                                        "LEFT JOIN (SELECT clip_id, "
                                        "SUM(CASE WHEN is_like=1 THEN 1 ELSE 0 END) AS plat_pl, "
                                        "SUM(CASE WHEN is_like=0 THEN 1 ELSE 0 END) AS plat_pd "
                                        "FROM likes GROUP BY clip_id) _plat_l ON _plat_l.clip_id=c.id ";

void append_trends_period_filter(std::ostringstream &sql, TimePeriod period, const char *twitch_dt) {
    switch (period) {
    case TimePeriod::Today:
        sql << " AND " << twitch_dt << " >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 1 DAY)";
        sql << " AND c.view_count >= 2";
        break;
    case TimePeriod::Week:
        sql << " AND " << twitch_dt << " >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 7 DAY)";
        sql << " AND c.view_count >= 10";
        break;
    case TimePeriod::Month:
        sql << " AND " << twitch_dt << " >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 30 DAY)";
        sql << " AND c.view_count >= 30";
        break;
    case TimePeriod::All:
        sql << " AND c.view_count >= 50";
        break;
    }
}

} // namespace

ClipRepository::ClipRepository(database::ConnectionPool &pool) : pool_(pool) {}

models::Clip ClipRepository::row_to_clip(MYSQL_ROW row, MYSQL_RES *res) {
    auto idx = [&](const char *name) -> int {
        unsigned int num = mysql_num_fields(res);
        MYSQL_FIELD *fields = mysql_fetch_fields(res);
        for (unsigned int i = 0; i < num; ++i)
            if (std::strcmp(fields[i].name, name) == 0)
                return static_cast<int>(i);
        return -1;
    };

    models::Clip c;
    c.id = std::stoull(row[idx("id")]);
    c.twitch_clip_id = row[idx("twitch_clip_id")] ? row[idx("twitch_clip_id")] : "";
    c.title = row[idx("title")] ? row[idx("title")] : "";
    c.thumbnail_url = row[idx("thumbnail_url")] ? row[idx("thumbnail_url")] : "";
    c.embed_url = row[idx("embed_url")] ? row[idx("embed_url")] : "";
    c.duration = std::stof(row[idx("duration")]);
    c.view_count = static_cast<uint32_t>(std::stoul(row[idx("view_count")]));
    c.like_count = std::stoi(row[idx("like_count")]);
    c.dislike_count = std::stoi(row[idx("dislike_count")]);
    c.broadcaster_name = row[idx("broadcaster_name")] ? row[idx("broadcaster_name")] : "";
    c.game_id = row[idx("game_id")] ? std::stoull(row[idx("game_id")]) : 0;
    c.author_id = row[idx("author_id")] ? std::stoull(row[idx("author_id")]) : 0;
    int ctid = idx("clip_creator_twitch_id");
    if (ctid >= 0 && row[ctid])
        c.creator_twitch_id = row[ctid];
    int clgn = idx("clip_creator_login");
    if (clgn >= 0 && row[clgn])
        c.creator_login = row[clgn];
    c.created_at = row[idx("created_at")] ? row[idx("created_at")] : "";

    int gn = idx("game_name");
    if (gn >= 0 && row[gn])
        c.game_name = row[gn];

    int ba = idx("broadcaster_avatar");
    if (ba >= 0 && row[ba])
        c.broadcaster_avatar = row[ba];
    int bi = idx("broadcaster_id");
    if (bi >= 0 && row[bi])
        c.broadcaster_id = row[bi];

    int vi = idx("vod_id");
    if (vi >= 0 && row[vi])
        c.vod_id = row[vi];

    int vo = idx("vod_offset");
    if (vo >= 0 && row[vo])
        c.vod_offset = std::stoi(row[vo]);

    int tc = idx("twitch_created_at");
    if (tc >= 0 && row[tc])
        c.twitch_created_at = row[tc];

    int login = idx("broadcaster_login");
    if (login >= 0 && row[login])
        c.broadcaster_login = row[login];
    int bl = idx("broadcaster_lang");
    if (bl >= 0 && row[bl] && row[bl][0])
        c.broadcaster_lang = row[bl];

    int tf = idx("title_fingerprint");
    if (tf >= 0 && row[tf])
        c.title_fingerprint = row[tf];

    int source = idx("classification_source");
    if (source >= 0 && row[source])
        c.classification_source = row[source];
    int im = idx("is_mature");
    if (im >= 0 && row[im])
        c.is_mature = (std::stoi(row[im]) != 0);

    int pl = idx("promo_lbl");
    if (pl >= 0 && row[pl] && row[pl][0])
        c.promo_label = row[pl];

    return c;
}

std::vector<std::string> ClipRepository::load_tags(uint64_t clip_id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT t.name FROM tags t JOIN clip_tags ct ON t.id=ct.tag_id WHERE ct.clip_id=" +
                      std::to_string(clip_id);
    if (database::query(conn.raw(), sql.c_str()))
        return {};

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};

    std::vector<std::string> tags;
    while (MYSQL_ROW row = mysql_fetch_row(res))
        if (row[0])
            tags.emplace_back(row[0]);

    res.reset();
    return tags;
}

void ClipRepository::fill_tags_bulk(std::vector<models::Clip *> &clips) {
    if (clips.empty())
        return;
    std::vector<uint64_t> ids;
    ids.reserve(clips.size());
    for (auto *p : clips) {
        if (p)
            ids.push_back(p->id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    database::ScopedConnection conn(pool_);
    std::ostringstream sql;
    sql << "SELECT ct.clip_id, GROUP_CONCAT(t.name ORDER BY t.name SEPARATOR ',') AS tag_csv "
        << "FROM clip_tags ct INNER JOIN tags t ON t.id=ct.tag_id WHERE ct.clip_id IN (";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i)
            sql << ",";
        sql << ids[i];
    }
    sql << ") GROUP BY ct.clip_id";
    if (database::query(conn.raw(), sql.str().c_str()))
        return;

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return;

    std::unordered_map<uint64_t, std::vector<std::string>> by_id;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        if (!row[0])
            continue;
        uint64_t cid = std::stoull(row[0]);
        std::vector<std::string> tags;
        if (row[1]) {
            std::string csv = row[1];
            std::string cur;
            for (char ch : csv) {
                if (ch == ',') {
                    if (!cur.empty())
                        tags.push_back(std::move(cur));
                    cur.clear();
                } else
                    cur += ch;
            }
            if (!cur.empty())
                tags.push_back(std::move(cur));
        }
        by_id[cid] = std::move(tags);
    }
    res.reset();

    for (auto *p : clips) {
        if (!p)
            continue;
        auto it = by_id.find(p->id);
        if (it != by_id.end())
            p->tags = it->second;
        else
            p->tags.clear();
    }
}

std::optional<models::Clip> ClipRepository::find_by_id(uint64_t id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT c.*, g.name AS game_name FROM clips c "
                      "LEFT JOIN games g ON c.game_id=g.id WHERE c.id=" +
                      std::to_string(id) + " AND c.unavailable_at IS NULL AND c.moderation_hidden=0";
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

    auto clip = row_to_clip(row, res);
    res.reset();
    conn.release();
    clip.tags = load_tags(clip.id);
    return clip;
}

std::vector<models::Clip> ClipRepository::find_all(uint32_t limit, uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT c.*, g.name AS game_name FROM clips c "
                      "LEFT JOIN games g ON c.game_id=g.id "
                      "ORDER BY c.created_at DESC LIMIT " +
                      std::to_string(limit) + " OFFSET " + std::to_string(offset);
    if (database::query(conn.raw(), sql.c_str()))
        return {};

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};

    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }

    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    fill_tags_bulk(tag_targets);
    return clips;
}

std::vector<models::Clip> ClipRepository::find_filtered(const ClipFilter &filter, uint32_t limit,
                                                        uint32_t offset, const FeedMuteSet *mutes) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    auto terms = split_terms(filter.search);
    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id=g.id "
        << "LEFT JOIN " << promo_boost_subquery("'all','feed'") << " ON promo.clip_id=c.id";

    if (!filter.tag.empty())
        sql << " JOIN clip_tags ct ON c.id=ct.clip_id JOIN tags t ON ct.tag_id=t.id";

    sql << " WHERE 1=1";

    append_game_filter(sql, filter.game_ids);

    if (!filter.tag.empty())
        sql << " AND t.name='" << esc(filter.tag) << "'";
    if (!filter.broadcaster_name.empty())
        sql << " AND c.broadcaster_name='" << esc(filter.broadcaster_name) << "'";
    if (!filter.search.empty()) {
        sql << " AND (";
        if (!terms.empty()) {
            for (size_t i = 0; i < terms.size(); ++i) {
                if (i)
                    sql << " OR ";
                sql << "c.title LIKE '%" << esc(terms[i]) << "%' "
                    << "OR c.broadcaster_name LIKE '%" << esc(terms[i]) << "%' "
                    << "OR c.clip_creator_login LIKE '%" << esc(terms[i]) << "%'";
            }
        } else {
            sql << "c.title LIKE '%" << esc(filter.search) << "%' "
                << "OR c.broadcaster_name LIKE '%" << esc(filter.search) << "%' "
                << "OR c.clip_creator_login LIKE '%" << esc(filter.search) << "%'";
        }
        sql << ")";
    }

    const char *twitch_dt = "STR_TO_DATE(c.twitch_created_at, '%Y-%m-%dT%H:%i:%sZ')";
    switch (filter.period) {
    case TimePeriod::Today:
        sql << " AND " << twitch_dt << " >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 1 DAY)";
        sql << " AND c.view_count >= 2";
        break;
    case TimePeriod::Week:
        sql << " AND " << twitch_dt << " >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 7 DAY)";
        sql << " AND c.view_count >= 10";
        break;
    case TimePeriod::Month:
        sql << " AND " << twitch_dt << " >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 30 DAY)";
        sql << " AND c.view_count >= 30";
        break;
    case TimePeriod::All:
        // For category/channel/search we should not hide low-view clips.
        if (!filter.game_ids.empty() || !filter.broadcaster_name.empty() || !filter.search.empty())
            sql << " AND c.view_count >= 2";
        else
            sql << " AND c.view_count >= 50";
        break;
    }

    std::string search_score;
    if (!filter.search.empty()) {
        std::ostringstream score;
        score << "(";
        if (!terms.empty()) {
            for (size_t i = 0; i < terms.size(); ++i) {
                if (i)
                    score << " + ";
                score << "(CASE WHEN c.title LIKE '%" << esc(terms[i]) << "%' THEN 30 ELSE 0 END)"
                      << " + (CASE WHEN c.broadcaster_name LIKE '%" << esc(terms[i])
                      << "%' THEN 12 ELSE 0 END)"
                      << " + (CASE WHEN c.clip_creator_login LIKE '%" << esc(terms[i])
                      << "%' THEN 10 ELSE 0 END)";
            }
        } else {
            score << "(CASE WHEN c.title LIKE '%" << esc(filter.search) << "%' THEN 30 ELSE 0 END)"
                  << " + (CASE WHEN c.broadcaster_name LIKE '%" << esc(filter.search)
                  << "%' THEN 12 ELSE 0 END)"
                  << " + (CASE WHEN c.clip_creator_login LIKE '%" << esc(filter.search)
                  << "%' THEN 10 ELSE 0 END)";
        }
        score << " + (CASE WHEN c.title LIKE '%" << esc(filter.search) << "%' THEN 18 ELSE 0 END)"
              << " + (CASE WHEN c.broadcaster_name LIKE '%" << esc(filter.search) << "%' THEN 8 ELSE 0 END)"
              << " + (CASE WHEN c.clip_creator_login LIKE '%" << esc(filter.search)
              << "%' THEN 6 ELSE 0 END)";
        score << ")";
        search_score = score.str();
    }

    append_public_feed_filters(sql, mutes, esc);

    // Сортировка (при поиске сначала релевантность, потом выбранная сортировка)
    sql << " ORDER BY ";
    if (!search_score.empty())
        sql << search_score << " DESC, COALESCE(promo.promo_boost,0) DESC, ";
    switch (filter.sort) {
    case ClipSort::Popular:
        sql << "(c.view_count * 1.0 + COALESCE(promo.promo_boost, 0) * 14.0) DESC, " << twitch_dt << " DESC";
        break;
    case ClipSort::Recent:
        sql << twitch_dt << " DESC, COALESCE(promo.promo_boost,0) DESC, c.view_count DESC";
        break;
    case ClipSort::Duration:
        sql << "c.duration DESC, COALESCE(promo.promo_boost,0) DESC, c.view_count DESC";
        break;
    }

    sql << " LIMIT " << limit << " OFFSET " << offset;

    if (database::query(conn.raw(), sql.str().c_str()))
        return {};

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};

    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }

    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    fill_tags_bulk(tag_targets);
    return clips;
}

std::optional<uint32_t> ClipRepository::popular_feed_position(uint64_t clip_id, TimePeriod period,
                                                              const FeedMuteSet *mutes, uint32_t cap) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    const char *twitch_dt = "STR_TO_DATE(c.twitch_created_at, '%Y-%m-%dT%H:%i:%sZ')";

    std::ostringstream sql;
    sql << "SELECT c.id FROM clips c "
        << "LEFT JOIN " << promo_boost_subquery("'all','feed'") << " ON promo.clip_id=c.id "
        << "WHERE 1=1 ";
    append_trends_period_filter(sql, period, twitch_dt);
    append_public_feed_filters(sql, mutes, esc);
    sql << " ORDER BY (c.view_count * 1.0 + COALESCE(promo.promo_boost, 0) * 14.0) DESC, " << twitch_dt
        << " DESC LIMIT " << cap;

    if (database::query(conn.raw(), sql.str().c_str()))
        return std::nullopt;

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return std::nullopt;

    uint32_t pos = 0;
    std::optional<uint32_t> found;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        ++pos;
        if (!row[0])
            continue;
        if (std::stoull(row[0]) == clip_id) {
            found = pos;
            break;
        }
    }
    res.reset();
    return found;
}

std::vector<models::Clip> ClipRepository::search(const std::string &query, uint32_t limit, uint32_t offset,
                                                 const FeedMuteSet *mutes) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string q = esc(query);
    auto terms = split_terms(query);
    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id=g.id "
        << "WHERE c.view_count >= 2 AND ("
        << "MATCH(c.title, c.broadcaster_name) AGAINST('" << q << "' IN NATURAL LANGUAGE MODE) ";
    if (!terms.empty()) {
        for (const auto &term : terms) {
            sql << "OR c.title LIKE '%" << esc(term) << "%' "
                << "OR c.broadcaster_name LIKE '%" << esc(term) << "%' "
                << "OR c.clip_creator_login LIKE '%" << esc(term) << "%' ";
        }
    } else {
        sql << "OR c.title LIKE '%" << q << "%' "
            << "OR c.broadcaster_name LIKE '%" << q << "%' "
            << "OR c.clip_creator_login LIKE '%" << q << "%' ";
    }
    sql << ") ";
    append_public_feed_filters(sql, mutes, esc);

    std::ostringstream score;
    score << "(";
    if (!terms.empty()) {
        for (size_t i = 0; i < terms.size(); ++i) {
            if (i)
                score << " + ";
            score << "(CASE WHEN c.title LIKE '%" << esc(terms[i]) << "%' THEN 35 ELSE 0 END)"
                  << " + (CASE WHEN c.broadcaster_name LIKE '%" << esc(terms[i]) << "%' THEN 14 ELSE 0 END)"
                  << " + (CASE WHEN c.clip_creator_login LIKE '%" << esc(terms[i])
                  << "%' THEN 12 ELSE 0 END)";
        }
    } else {
        score << "(CASE WHEN c.title LIKE '%" << q << "%' THEN 35 ELSE 0 END)"
              << " + (CASE WHEN c.broadcaster_name LIKE '%" << q << "%' THEN 14 ELSE 0 END)"
              << " + (CASE WHEN c.clip_creator_login LIKE '%" << q << "%' THEN 12 ELSE 0 END)";
    }
    score << " + (CASE WHEN c.title LIKE '%" << q << "%' THEN 20 ELSE 0 END)"
          << " + (CASE WHEN c.broadcaster_name LIKE '%" << q << "%' THEN 10 ELSE 0 END)"
          << " + (CASE WHEN c.clip_creator_login LIKE '%" << q << "%' THEN 8 ELSE 0 END)"
          << ")";

    sql << " ORDER BY " << score.str() << " DESC, c.view_count DESC, "
        << "STR_TO_DATE(c.twitch_created_at, '%Y-%m-%dT%H:%i:%sZ') DESC "
        << "LIMIT " << limit << " OFFSET " << offset;

    if (database::query(conn.raw(), sql.str().c_str()))
        return {};

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};

    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }

    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    fill_tags_bulk(tag_targets);
    return clips;
}

std::vector<models::Clip> ClipRepository::find_discussed_clips(uint32_t hours, uint32_t limit,
                                                               const FeedMuteSet *mutes, bool with_tags) {
    if (hours < 1)
        hours = 1;
    if (hours > 168)
        hours = 168;
    if (limit < 1)
        limit = 1;
    if (limit > 48)
        limit = 48;
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id=g.id "
        << "INNER JOIN ("
        << "SELECT clip_id, COUNT(*) AS discussion_count FROM comments "
        << "WHERE created_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL " << hours << " HOUR) "
        << "GROUP BY clip_id HAVING discussion_count >= 2"
        << ") dc ON dc.clip_id=c.id "
        << "WHERE 1=1 ";
    append_public_feed_filters(sql, mutes, esc);
    sql << " ORDER BY dc.discussion_count DESC LIMIT " << limit;

    if (database::query(conn.raw(), sql.str().c_str()))
        return {};

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};

    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }
    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    if (with_tags)
        fill_tags_bulk(tag_targets);
    return clips;
}

std::vector<models::Clip> ClipRepository::find_continue_watching(uint64_t user_id, uint32_t limit,
                                                                 const FeedMuteSet *mutes, bool with_tags) {
    if (user_id == 0)
        return {};
    if (limit < 1)
        limit = 1;
    if (limit > 36)
        limit = 36;
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM user_clip_engagement uce "
        << "INNER JOIN clips c ON c.id = uce.clip_id "
        << "LEFT JOIN games g ON c.game_id = g.id "
        << "WHERE uce.user_id=" << user_id << " AND uce.watch_time_sec >= 5 AND uce.completion_ratio < 0.88 "
        << " AND uce.updated_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 45 DAY) ";
    append_public_feed_filters(sql, mutes, esc);
    sql << " ORDER BY uce.updated_at DESC LIMIT " << limit;

    if (database::query(conn.raw(), sql.str().c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }
    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    if (with_tags)
        fill_tags_bulk(tag_targets);
    return clips;
}

std::vector<models::Clip>
ClipRepository::find_clips_from_visited_channels(uint64_t user_id, uint32_t visit_days, uint32_t clip_days,
                                                 uint32_t limit, const FeedMuteSet *mutes, bool with_tags) {
    if (user_id == 0)
        return {};
    if (visit_days < 1)
        visit_days = 1;
    if (visit_days > 60)
        visit_days = 60;
    if (clip_days < 1)
        clip_days = 1;
    if (clip_days > 30)
        clip_days = 30;
    if (limit < 1)
        limit = 1;
    if (limit > 36)
        limit = 36;
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id = g.id "
        << "WHERE c.broadcaster_id IN ("
        << "SELECT DISTINCT c2.broadcaster_id FROM view_history vh "
        << "INNER JOIN clips c2 ON c2.id = vh.clip_id "
        << "WHERE vh.user_id=" << user_id << " AND vh.viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL "
        << visit_days << " DAY) "
        << "AND c2.broadcaster_id IS NOT NULL AND TRIM(c2.broadcaster_id) <> '') "
        << "AND COALESCE(c.twitch_created_ts, c.created_at) >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL "
        << clip_days << " DAY) ";
    append_public_feed_filters(sql, mutes, esc);
    sql << " ORDER BY COALESCE(c.twitch_created_ts, c.created_at) DESC LIMIT " << limit;

    if (database::query(conn.raw(), sql.str().c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }
    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    if (with_tags)
        fill_tags_bulk(tag_targets);
    return clips;
}

std::optional<models::Clip> ClipRepository::find_random_clip(const FeedMuteSet *mutes, bool with_tags) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id = g.id "
        << "WHERE c.view_count >= 2 ";
    append_public_feed_filters(sql, mutes, esc);
    sql << " ORDER BY RAND() LIMIT 1";

    if (database::query(conn.raw(), sql.str().c_str()))
        return std::nullopt;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return std::nullopt;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        res.reset();
        return std::nullopt;
    }
    auto c = row_to_clip(row, res);
    res.reset();
    conn.release();
    if (with_tags)
        c.tags = load_tags(c.id);
    return c;
}

std::vector<models::Clip> ClipRepository::find_hot(uint32_t limit,
                                                   const std::vector<uint64_t> &exclude_clip_ids,
                                                   const std::vector<uint64_t> &preferred_games,
                                                   const std::vector<std::string> &preferred_broadcasters,
                                                   const RecommendationWeights &weights,
                                                   const FeedMuteSet *mutes, bool with_tags) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    const char *twitch_dt = "STR_TO_DATE(c.twitch_created_at, '%Y-%m-%dT%H:%i:%sZ')";
    const char *clip_when = "COALESCE(c.twitch_created_ts, c.created_at)";

    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id=g.id " << k_clip_platform_aggr_join << "LEFT JOIN "
        << promo_boost_subquery("'all','hot','feed'") << " ON promo.clip_id=c.id "
        << "WHERE ("
        << "(c.twitch_created_ts IS NOT NULL AND c.twitch_created_ts >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 7 "
           "DAY)) "
        << "OR (c.twitch_created_ts IS NULL AND " << twitch_dt
        << " >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 7 DAY)) "
        << ") ";
    if (!exclude_clip_ids.empty()) {
        sql << "AND c.id NOT IN (";
        append_in_numeric(sql, exclude_clip_ids);
        sql << ") ";
    }
    append_public_feed_filters(sql, mutes, esc);
    sql << "ORDER BY ("
        << "(c.view_count / GREATEST(1, TIMESTAMPDIFF(HOUR, " << clip_when << ", UTC_TIMESTAMP())))"
        << " + LOG(1 + GREATEST(0, COALESCE(_plat_v.plat_pv,0))) * " << weights.rec_platform_view_weight
        << " + LOG(1 + GREATEST(0, COALESCE(_plat_c.plat_pc,0))) * " << weights.rec_platform_comment_weight
        << " + COALESCE(_plat_l.plat_pl,0) * " << (weights.rec_like_weight * 0.35)
        << " + COALESCE(promo.promo_boost, 0)";
    if (!preferred_games.empty()) {
        sql << " + CASE WHEN c.game_id IN (";
        append_in_numeric(sql, preferred_games);
        sql << ") THEN " << weights.hot_pref_game_boost << " ELSE 0 END";
    }
    if (!preferred_broadcasters.empty()) {
        sql << " + CASE WHEN c.broadcaster_id IN (";
        append_in_quoted(sql, preferred_broadcasters, esc);
        sql << ") THEN " << weights.hot_pref_broadcaster_boost << " ELSE 0 END";
    }
    sql << ") DESC, c.view_count DESC LIMIT " << limit;

    if (database::query(conn.raw(), sql.str().c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }
    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    if (with_tags)
        fill_tags_bulk(tag_targets);
    return clips;
}

std::vector<models::Clip>
ClipRepository::find_fresh_random(uint32_t limit, const std::vector<uint64_t> &exclude_clip_ids,
                                  const std::vector<std::string> &tracked_broadcaster_ids,
                                  const FeedMuteSet *mutes, bool with_tags) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    const char *twitch_dt = "STR_TO_DATE(c.twitch_created_at, '%Y-%m-%dT%H:%i:%sZ')";

    RecommendationWeights plat_w;
    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id=g.id " << k_clip_platform_aggr_join << "LEFT JOIN "
        << promo_boost_subquery("'all','fresh','feed'") << " ON promo.clip_id=c.id "
        << "WHERE ("
        << "(c.twitch_created_ts IS NOT NULL AND c.twitch_created_ts >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 2 "
           "DAY)) "
        << "OR (c.twitch_created_ts IS NULL AND " << twitch_dt
        << " >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 2 DAY)) "
        << ") ";
    if (!exclude_clip_ids.empty()) {
        sql << "AND c.id NOT IN (";
        append_in_numeric(sql, exclude_clip_ids);
        sql << ") ";
    }
    if (!tracked_broadcaster_ids.empty()) {
        sql << "AND c.broadcaster_id IN (";
        append_in_quoted(sql, tracked_broadcaster_ids, esc);
        sql << ") ";
    }
    append_public_feed_filters(sql, mutes, esc);
    sql << "ORDER BY (COALESCE(promo.promo_boost,0) * 10.0 + LEAST(500.0, c.view_count) * 0.12 "
        << "+ LOG(1 + GREATEST(0, COALESCE(_plat_v.plat_pv,0))) * " << plat_w.rec_platform_view_weight
        << "+ LOG(1 + GREATEST(0, COALESCE(_plat_c.plat_pc,0))) * " << plat_w.rec_platform_comment_weight
        << "+ COALESCE(_plat_l.plat_pl,0) * " << (plat_w.rec_like_weight * 0.25) << "+ RAND() * 42.0) DESC "
        << "LIMIT " << limit;

    if (database::query(conn.raw(), sql.str().c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }
    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    if (with_tags)
        fill_tags_bulk(tag_targets);
    return clips;
}

std::vector<models::Clip> ClipRepository::find_recommended(
    uint32_t limit, const std::vector<uint64_t> &exclude_clip_ids,
    const std::vector<uint64_t> &preferred_games, const std::vector<std::string> &preferred_broadcasters,
    const std::vector<std::string> &similar_broadcasters, const RecommendationWeights &weights,
    uint64_t affinity_user_id, const FeedMuteSet *mutes, bool with_tags) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };

    const char *clip_age_h =
        "TIMESTAMPDIFF(HOUR, COALESCE(c.twitch_created_ts, c.created_at), UTC_TIMESTAMP())";

    std::ostringstream aff_join;
    std::ostringstream aff_term;
    if (affinity_user_id != 0) {
        aff_join << " LEFT JOIN ("
                 << "SELECT DISTINCT c3.game_id AS aff_gid FROM view_history vh "
                 << "INNER JOIN clips c3 ON c3.id=vh.clip_id AND c3.game_id IS NOT NULL "
                 << "WHERE vh.user_id=" << affinity_user_id
                 << " AND vh.viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 14 DAY)"
                 << ") aff ON aff.aff_gid=c.game_id AND c.game_id IS NOT NULL";
        aff_term << " + CASE WHEN aff.aff_gid IS NOT NULL THEN " << weights.rec_affinity_recent_game_boost
                 << " ELSE 0 END";
    }

    std::ostringstream eng_join;
    std::ostringstream eng_term;
    if (affinity_user_id != 0) {
        eng_join
            << " LEFT JOIN ("
            << "SELECT DISTINCT c5.game_id AS eng_gid FROM user_clip_engagement uce "
            << "INNER JOIN clips c5 ON c5.id=uce.clip_id AND c5.game_id IS NOT NULL "
            << "WHERE uce.user_id=" << affinity_user_id
            << " AND uce.watch_time_sec>=28 AND uce.updated_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 21 DAY)"
            << ") eng ON eng.eng_gid=c.game_id AND c.game_id IS NOT NULL";
        eng_term << " + CASE WHEN eng.eng_gid IS NOT NULL THEN " << weights.rec_engagement_game_boost
                 << " ELSE 0 END";
    }

    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id=g.id " << k_clip_platform_aggr_join
        << "LEFT JOIN feature_store_clip f ON f.clip_id=c.id " << aff_join.str() << eng_join.str()
        << " LEFT JOIN " << promo_boost_subquery("'all','recommended','feed'") << " ON promo.clip_id=c.id "
        << "WHERE (c.view_count >= 2 OR COALESCE(_plat_v.plat_pv,0) >= 2 OR COALESCE(_plat_c.plat_pc,0) >= 1 "
           "OR COALESCE(_plat_l.plat_pl,0) >= 1) "
        << "AND ((c.twitch_created_ts IS NOT NULL AND c.twitch_created_ts >= DATE_SUB(UTC_TIMESTAMP(), "
           "INTERVAL 56 DAY)) "
        << "OR (c.twitch_created_ts IS NULL AND c.created_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 56 "
           "DAY))) ";
    if (!exclude_clip_ids.empty()) {
        sql << "AND c.id NOT IN (";
        append_in_numeric(sql, exclude_clip_ids);
        sql << ") ";
    }
    append_public_feed_filters(sql, mutes, esc);
    sql << "ORDER BY (c.view_count * " << weights.rec_view_weight << " + COALESCE(_plat_l.plat_pl,0) * "
        << weights.rec_like_weight << " - COALESCE(_plat_l.plat_pd,0) * " << weights.rec_dislike_weight
        << " + LOG(1 + GREATEST(0, COALESCE(_plat_v.plat_pv,0))) * " << weights.rec_platform_view_weight
        << " + LOG(1 + GREATEST(0, COALESCE(_plat_c.plat_pc,0))) * " << weights.rec_platform_comment_weight
        << " + GREATEST(0, " << weights.rec_freshness_weight << " - " << clip_age_h << ")"
        << " + COALESCE(f.velocity_score, c.view_count / GREATEST(1.0, " << clip_age_h << "/24.0)) * "
        << weights.rec_fs_velocity_weight
        << " + COALESCE(f.er_like, COALESCE(_plat_l.plat_pl,0) / GREATEST(1, COALESCE(_plat_v.plat_pv,0))) * "
        << weights.rec_fs_er_weight << " + COALESCE(f.freshness_decay, EXP(-GREATEST(0, " << clip_age_h
        << ") / 168.0)) * " << weights.rec_fs_fresh_decay_weight << " + COALESCE(promo.promo_boost, 0) * 1.4";
    if (!preferred_games.empty()) {
        sql << " + CASE WHEN c.game_id IN (";
        append_in_numeric(sql, preferred_games);
        sql << ") THEN " << weights.rec_pref_game_boost << " ELSE 0 END";
    }
    if (!preferred_broadcasters.empty()) {
        sql << " + CASE WHEN c.broadcaster_id IN (";
        append_in_quoted(sql, preferred_broadcasters, esc);
        sql << ") THEN " << weights.rec_pref_broadcaster_boost << " ELSE 0 END";
    }
    if (!similar_broadcasters.empty()) {
        sql << " + CASE WHEN c.broadcaster_id IN (";
        append_in_quoted(sql, similar_broadcasters, esc);
        sql << ") THEN " << weights.rec_similar_broadcaster_boost << " ELSE 0 END";
    }
    sql << aff_term.str() << eng_term.str() << " + RAND() * " << weights.rec_random_weight
        << ") DESC, c.view_count DESC LIMIT " << limit;

    if (database::query(conn.raw(), sql.str().c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }
    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    if (with_tags)
        fill_tags_bulk(tag_targets);
    return clips;
}

std::vector<models::Clip> ClipRepository::find_related_clips(uint64_t seed_clip_id, uint64_t viewer_user_id,
                                                             uint32_t limit, const FeedMuteSet *mutes,
                                                             uint32_t mix_seed,
                                                             const std::vector<uint64_t> *extra_exclude) {
    auto seed = find_by_id(seed_clip_id);
    if (!seed)
        return {};

    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };

    auto append_extra_ids = [&](std::ostringstream &q) {
        if (!extra_exclude || extra_exclude->empty())
            return;
        q << "AND c.id NOT IN (";
        append_in_numeric(q, *extra_exclude);
        q << ") ";
    };

    const uint64_t gid = seed->game_id;
    const std::string &bcid = seed->broadcaster_id;
    const std::string &fp = seed->title_fingerprint;

    std::vector<models::Clip> pool;
    pool.reserve(256);
    const uint32_t pool_cap = std::min<uint32_t>(std::max<uint32_t>(limit * 14U, 140U), 320U);

    auto have_id = [&](uint64_t id) {
        for (const auto &x : pool)
            if (x.id == id)
                return true;
        return false;
    };

    {
        std::ostringstream sql;
        sql << "SELECT c.*, g.name AS game_name, "
            << "(cv.co * 14.0 ";
        if (gid > 0)
            sql << "+ CASE WHEN c.game_id=" << gid << " THEN 26.0 ELSE 0 END ";
        if (!bcid.empty())
            sql << "+ CASE WHEN c.broadcaster_id='" << esc(bcid) << "' THEN 18.0 ELSE 0 END ";
        if (!fp.empty())
            sql << "+ CASE WHEN c.title_fingerprint='" << esc(fp)
                << "' AND c.title_fingerprint<>'' THEN 16.0 ELSE 0 END ";
        sql << "+ LOG(1+c.view_count) * 0.65 + COALESCE(promo.promo_boost,0) * 1.5"
            << ") AS _rel_score, promo.promo_lbl AS promo_lbl "
            << "FROM ("
            << "SELECT v2.clip_id AS cid, COUNT(*) AS co FROM view_history v1 "
            << "INNER JOIN view_history v2 ON v1.user_id=v2.user_id AND v2.clip_id<>v1.clip_id "
            << "WHERE v1.clip_id=" << seed_clip_id
            << " AND v2.viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 45 DAY) "
            << "GROUP BY v2.clip_id HAVING co>=1 ORDER BY co DESC LIMIT 220"
            << ") cv "
            << "INNER JOIN clips c ON c.id=cv.cid "
            << "LEFT JOIN games g ON g.id=c.game_id "
            << "LEFT JOIN " << promo_boost_label_subquery("'all','related','feed'")
            << " ON promo.clip_id=c.id "
            << "WHERE c.unavailable_at IS NULL AND c.moderation_hidden=0 AND c.id<>" << seed_clip_id << " ";
        append_extra_ids(sql);
        if (viewer_user_id != 0) {
            sql << "AND NOT EXISTS (SELECT 1 FROM view_history vx WHERE vx.user_id=" << viewer_user_id
                << " AND vx.clip_id=c.id AND vx.viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 5 DAY) LIMIT "
                   "1) ";
        }
        append_public_feed_filters(sql, mutes, esc);
        sql << "ORDER BY _rel_score DESC LIMIT " << pool_cap;

        if (database::query(conn.raw(), sql.str().c_str()) == 0) {
            database::Result res(mysql_store_result(conn.raw()));
            if (res) {
                while (MYSQL_ROW row = mysql_fetch_row(res)) {
                    auto c = row_to_clip(row, res);

                    pool.push_back(std::move(c));
                }
                res.reset();
            }
        }
    }

    if (pool.size() < pool_cap) {
        std::ostringstream fb;
        fb << "SELECT c.*, g.name AS game_name, promo.promo_lbl AS promo_lbl FROM clips c "
           << "LEFT JOIN games g ON g.id=c.game_id "
           << "LEFT JOIN " << promo_boost_label_subquery("'all','related','feed'")
           << " ON promo.clip_id=c.id "
           << "WHERE c.unavailable_at IS NULL AND c.moderation_hidden=0 AND c.id<>" << seed_clip_id << " ";
        append_extra_ids(fb);
        if (gid > 0)
            fb << "AND c.game_id=" << gid << " ";
        else if (!bcid.empty())
            fb << "AND c.broadcaster_id='" << esc(bcid) << "' ";
        else
            fb << "AND 1=0 ";

        if (viewer_user_id != 0) {
            fb << "AND NOT EXISTS (SELECT 1 FROM view_history vx WHERE vx.user_id=" << viewer_user_id
               << " AND vx.clip_id=c.id AND vx.viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 5 DAY) LIMIT "
                  "1) ";
        }
        append_public_feed_filters(fb, mutes, esc);
        fb << "ORDER BY (c.view_count * 1.0 + COALESCE(promo.promo_boost,0) * 12.0) DESC LIMIT " << pool_cap;

        if (database::query(conn.raw(), fb.str().c_str()) == 0) {
            database::Result res(mysql_store_result(conn.raw()));
            if (res) {
                while (MYSQL_ROW row = mysql_fetch_row(res)) {
                    auto c = row_to_clip(row, res);
                    if (have_id(c.id))
                        continue;

                    pool.push_back(std::move(c));
                    if (pool.size() >= pool_cap)
                        break;
                }
                res.reset();
            }
        }
    }

    if (pool.size() < pool_cap && gid == 0 && bcid.empty()) {
        std::ostringstream fb2;
        fb2 << "SELECT c.*, g.name AS game_name, promo.promo_lbl AS promo_lbl FROM clips c "
            << "LEFT JOIN games g ON g.id=c.game_id "
            << "LEFT JOIN " << promo_boost_label_subquery("'all','related','feed'")
            << " ON promo.clip_id=c.id "
            << "WHERE c.unavailable_at IS NULL AND c.moderation_hidden=0 AND c.id<>" << seed_clip_id << " ";
        append_extra_ids(fb2);
        if (viewer_user_id != 0) {
            fb2 << "AND NOT EXISTS (SELECT 1 FROM view_history vx WHERE vx.user_id=" << viewer_user_id
                << " AND vx.clip_id=c.id AND vx.viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 5 DAY) LIMIT "
                   "1) ";
        }
        append_public_feed_filters(fb2, mutes, esc);
        fb2 << "ORDER BY (c.view_count * 1.0 + COALESCE(promo.promo_boost,0) * 12.0) DESC LIMIT " << pool_cap;
        if (database::query(conn.raw(), fb2.str().c_str()) == 0) {
            database::Result res(mysql_store_result(conn.raw()));
            if (res) {
                while (MYSQL_ROW row = mysql_fetch_row(res)) {
                    auto c = row_to_clip(row, res);
                    if (have_id(c.id))
                        continue;

                    pool.push_back(std::move(c));
                    if (pool.size() >= pool_cap)
                        break;
                }
                res.reset();
            }
        }
    }

    std::random_device rd;
    std::seed_seq ss{mix_seed,
                     mix_seed ^ 0xA53CC5E5u,
                     static_cast<unsigned>(seed_clip_id & 0xFFFFFFFFu),
                     static_cast<unsigned>((seed_clip_id >> 32) & 0xFFFFFFFFu),
                     rd(),
                     rd()};
    std::mt19937 rng(ss);
    std::shuffle(pool.begin(), pool.end(), rng);
    if (pool.size() > limit)
        pool.resize(limit);
    conn.release();
    std::vector<models::Clip *> targets;
    for (auto &c : pool)
        targets.push_back(&c);
    fill_tags_bulk(targets);
    return pool;
}

std::vector<models::Clip> ClipRepository::find_public_clips_for_home_fallback(uint32_t limit,
                                                                              const FeedMuteSet *mutes) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id=g.id "
        << "WHERE 1=1 ";
    append_public_feed_filters(sql, mutes, esc);
    sql << " ORDER BY c.view_count DESC, c.id DESC LIMIT " << limit << " OFFSET 0";

    if (database::query(conn.raw(), sql.str().c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }
    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    fill_tags_bulk(tag_targets);
    return clips;
}

std::vector<models::Clip> ClipRepository::find_profile_clips(uint64_t user_id,
                                                             const std::string &user_twitch_id,
                                                             uint32_t limit, uint32_t offset,
                                                             const FeedMuteSet *mutes) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id=g.id "
        << "WHERE c.unavailable_at IS NULL AND c.moderation_hidden=0 AND (c.author_id=" << user_id;
    if (!user_twitch_id.empty())
        sql << " OR c.clip_creator_twitch_id='" << esc(user_twitch_id) << "'";
    sql << ") ";
    append_public_feed_filters(sql, mutes, esc);
    sql << " ORDER BY c.created_at DESC LIMIT " << limit << " OFFSET " << offset;
    if (database::query(conn.raw(), sql.str().c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }
    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    fill_tags_bulk(tag_targets);
    return clips;
}

std::optional<nlohmann::json>
ClipRepository::find_user_brief_by_twitch_id(const std::string &twitch_user_id) {
    if (twitch_user_id.empty())
        return std::nullopt;
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT id, username FROM users WHERE twitch_id='" +
                      database::escape(conn.raw(), twitch_user_id) + "' AND account_blocked=0 LIMIT 1";
    if (database::query(conn.raw(), sql.c_str()))
        return std::nullopt;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return std::nullopt;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row || !row[0]) {
        res.reset();
        return std::nullopt;
    }
    nlohmann::json j = {
        {"id", std::stoull(row[0])},
        {"username", row[1] ? std::string(row[1]) : ""},
    };
    res.reset();
    return j;
}

std::vector<models::Clip> ClipRepository::find_by_author(uint64_t author_id, uint32_t limit, uint32_t offset,
                                                         const FeedMuteSet *mutes) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id=g.id "
        << "WHERE c.author_id=" << author_id << " ";
    append_public_feed_filters(sql, mutes, esc);
    sql << " ORDER BY c.created_at DESC LIMIT " << limit << " OFFSET " << offset;
    if (database::query(conn.raw(), sql.str().c_str()))
        return {};

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};

    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }

    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    fill_tags_bulk(tag_targets);
    return clips;
}

std::vector<models::Clip> ClipRepository::find_personalized(const ClipFilter &filter,
                                                            const std::vector<std::string> &broadcaster_names,
                                                            uint32_t limit, uint32_t offset,
                                                            const FeedMuteSet *mutes) {
    if (broadcaster_names.empty())
        return {};
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };

    const char *twitch_dt = "STR_TO_DATE(c.twitch_created_at, '%Y-%m-%dT%H:%i:%sZ')";
    std::ostringstream body;
    if (!filter.tag.empty()) {
        body << "FROM clips c "
             << "LEFT JOIN games g ON c.game_id=g.id " << k_clip_platform_aggr_join
             << "JOIN clip_tags ct ON c.id=ct.clip_id "
             << "JOIN tags t ON ct.tag_id=t.id ";
    } else {
        body << "FROM clips c LEFT JOIN games g ON c.game_id=g.id " << k_clip_platform_aggr_join;
    }
    body << "WHERE 1=1 ";
    append_game_filter(body, filter.game_ids);
    if (!filter.tag.empty())
        body << "AND t.name='" << esc(filter.tag) << "' ";
    if (!filter.broadcaster_name.empty())
        body << "AND c.broadcaster_name='" << esc(filter.broadcaster_name) << "' ";
    if (!filter.search.empty())
        body << "AND (c.title LIKE '%" << esc(filter.search) << "%' OR c.broadcaster_name LIKE '%"
             << esc(filter.search) << "%') ";
    switch (filter.period) {
    case TimePeriod::Today:
        body << "AND " << twitch_dt << " >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 1 DAY) AND c.view_count >= 2 ";
        break;
    case TimePeriod::Week:
        body << "AND " << twitch_dt
             << " >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 7 DAY) AND c.view_count >= 10 ";
        break;
    case TimePeriod::Month:
        body << "AND " << twitch_dt
             << " >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 30 DAY) AND c.view_count >= 30 ";
        break;
    case TimePeriod::All:
        if (!filter.game_ids.empty() || !filter.broadcaster_name.empty() || !filter.search.empty())
            body << "AND c.view_count >= 2 ";
        else
            body << "AND c.view_count >= 50 ";
        break;
    }
    body << "AND c.broadcaster_name IN (";
    for (size_t i = 0; i < broadcaster_names.size(); ++i) {
        if (i)
            body << ",";
        body << "'" << esc(broadcaster_names[i]) << "'";
    }
    body << ") ";
    append_public_feed_filters(body, mutes, esc);

    std::ostringstream query;
    query << "SELECT c.*, g.name AS game_name, "
          << "(c.view_count * 0.4 + COALESCE(_plat_l.plat_pl,0) * 2.0 - COALESCE(_plat_l.plat_pd,0) * 1.5 "
          << "+ LOG(1 + GREATEST(0, COALESCE(_plat_v.plat_pv,0))) * 0.42 "
          << "+ LOG(1 + GREATEST(0, COALESCE(_plat_c.plat_pc,0))) * 1.75 "
          << "+ GREATEST(0, 100 - TIMESTAMPDIFF(HOUR, STR_TO_DATE(c.twitch_created_at, "
             "'%Y-%m-%dT%H:%i:%sZ'), UTC_TIMESTAMP()))) AS score "
          << body.str();
    switch (filter.sort) {
    case ClipSort::Recent:
        query << "ORDER BY " << twitch_dt << " DESC ";
        break;
    case ClipSort::Duration:
        query << "ORDER BY c.duration DESC ";
        break;
    case ClipSort::Popular:
    default:
        query << "ORDER BY score DESC, " << twitch_dt << " DESC ";
        break;
    }
    query << "LIMIT " << std::max<uint32_t>(limit * 8, offset + limit * 4);

    if (database::query(conn.raw(), query.str().c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};

    std::vector<models::Clip> ranked;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        ranked.push_back(std::move(c));
    }
    res.reset();

    std::unordered_map<std::string, std::deque<models::Clip>> by_channel;
    std::vector<std::string> channel_order;
    channel_order.reserve(broadcaster_names.size());
    for (auto &c : ranked) {
        if (by_channel.find(c.broadcaster_name) == by_channel.end())
            channel_order.push_back(c.broadcaster_name);
        by_channel[c.broadcaster_name].push_back(std::move(c));
    }

    std::vector<models::Clip> diversified;
    diversified.reserve(ranked.size());
    constexpr size_t kPerRoundPerChannel = 1;
    bool progress = true;
    while (progress) {
        progress = false;
        for (const auto &ch : channel_order) {
            auto it = by_channel.find(ch);
            if (it == by_channel.end())
                continue;
            for (size_t i = 0; i < kPerRoundPerChannel && !it->second.empty(); ++i) {
                diversified.push_back(std::move(it->second.front()));
                it->second.pop_front();
                progress = true;
            }
        }
    }

    if (offset >= diversified.size())
        return {};
    const size_t end = std::min(diversified.size(), static_cast<size_t>(offset + limit));
    return {diversified.begin() + static_cast<std::ptrdiff_t>(offset),
            diversified.begin() + static_cast<std::ptrdiff_t>(end)};
}

std::vector<models::Clip> ClipRepository::find_entity_clips(uint64_t user, const std::string &twitch,
                                                            const std::string &scope, uint32_t limit,
                                                            uint32_t offset, const FeedMuteSet *mutes) {
    if (scope != "all" && scope != "channel" && scope != "created")
        throw std::invalid_argument("scope must be all, channel or created");
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &value) { return database::escape(conn.raw(), value); };
    std::ostringstream sql;
    sql << "SELECT c.*,g.name game_name FROM clips c LEFT JOIN games g ON g.id=c.game_id WHERE (0";
    if (scope != "created" && !twitch.empty())
        sql << " OR c.broadcaster_id='" << esc(twitch) << "'";
    if (scope != "channel") {
        if (!twitch.empty())
            sql << " OR c.clip_creator_twitch_id='" << esc(twitch) << "'";
        if (user)
            sql << " OR c.author_id=" << user;
    }
    sql << ")";
    append_public_feed_filters(sql, mutes, esc);
    sql << " ORDER BY COALESCE(c.twitch_created_ts,c.created_at) DESC,c.id DESC LIMIT "
        << std::clamp(limit, 1u, 101u) << " OFFSET " << std::min(offset, 10000u);
    database::query(conn.raw(), sql.str().c_str());
    database::Result result(mysql_store_result(conn.raw()));
    std::vector<models::Clip> out;
    while (auto row = mysql_fetch_row(result))
        out.push_back(row_to_clip(row, result));
    result.reset();
    conn.release();
    std::vector<models::Clip *> targets;
    for (auto &clip : out)
        targets.push_back(&clip);
    fill_tags_bulk(targets);
    return out;
}

std::vector<models::Clip> ClipRepository::find_following_feed(uint64_t viewer, uint32_t limit,
                                                              uint32_t offset, const FeedMuteSet *mutes) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &value) { return database::escape(conn.raw(), value); };
    std::ostringstream sql;
    sql << "SELECT c.*,g.name game_name FROM clips c LEFT JOIN games g ON g.id=c.game_id WHERE EXISTS(SELECT "
           "1 FROM profile_follows pf JOIN profile_entities pe ON pe.profile_key=pf.profile_key LEFT JOIN "
           "user_profile_settings ups ON ups.user_id=pe.local_user_id WHERE pf.follower_user_id="
        << viewer << " AND(pe.local_user_id IS NULL OR pe.local_user_id=" << viewer
        << " OR COALESCE(ups.profile_visibility,'public')='public' OR(ups.profile_visibility='friends' AND "
           "EXISTS(SELECT 1 FROM friendships f WHERE f.user_id="
        << viewer
        << " AND f.friend_id=pe.local_user_id))) AND((pe.twitch_id IS NOT NULL "
           "AND(pe.twitch_id=c.broadcaster_id OR pe.twitch_id=c.clip_creator_twitch_id))OR(pe.local_user_id "
           "IS NOT NULL AND pe.local_user_id=c.author_id)))";
    append_public_feed_filters(sql, mutes, esc);
    sql << " ORDER BY COALESCE(c.twitch_created_ts,c.created_at) DESC,c.id DESC LIMIT "
        << std::clamp(limit, 1u, 101u) << " OFFSET " << std::min(offset, 10000u);
    database::query(conn.raw(), sql.str().c_str());
    database::Result result(mysql_store_result(conn.raw()));
    std::vector<models::Clip> clips;
    while (auto row = mysql_fetch_row(result))
        clips.push_back(row_to_clip(row, result));
    result.reset();
    conn.release();
    std::vector<models::Clip *> targets;
    for (auto &c : clips)
        targets.push_back(&c);
    fill_tags_bulk(targets);
    return clips;
}
std::vector<models::Clip> ClipRepository::find_friends_feed(uint64_t viewer_user_id, uint32_t limit,
                                                            uint32_t offset, const FeedMuteSet *mutes) {
    if (viewer_user_id == 0)
        return {};
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id=g.id "
        << "INNER JOIN ("
        << " SELECT vh.clip_id, MAX(vh.viewed_at) AS mv "
        << " FROM friendships s "
        << " INNER JOIN view_history vh ON vh.user_id = s.friend_id "
        << " WHERE s.user_id = " << viewer_user_id << " GROUP BY vh.clip_id"
        << ") t ON t.clip_id = c.id "
        << "WHERE 1=1 ";
    append_public_feed_filters(sql, mutes, esc);
    sql << " ORDER BY t.mv DESC LIMIT " << limit << " OFFSET " << offset;

    if (database::query(conn.raw(), sql.str().c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};

    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }
    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    fill_tags_bulk(tag_targets);
    return clips;
}

uint64_t ClipRepository::create(const models::Clip &clip) {
    database::ScopedConnection conn(pool_);
    uint64_t affected = 0;
    return create_on_connection(conn.raw(), clip, affected);
}

uint64_t ClipRepository::create_on_connection(MYSQL *conn, const models::Clip &clip, uint64_t &affected) {
    auto esc = [&](const std::string &s) { return database::escape(conn, s); };

    const std::string fp = clip.title_fingerprint.empty()
                               ? utils::compute_title_fingerprint(clip.title, clip.broadcaster_name)
                               : clip.title_fingerprint;

    std::ostringstream sql;
    sql << "INSERT INTO clips (discovery_bucket,twitch_clip_id, title, title_fingerprint, thumbnail_url, embed_url, duration, "
        << "view_count, broadcaster_name, broadcaster_id, broadcaster_avatar, broadcaster_lang, "
           "broadcaster_login, is_mature, classification_source, game_id, author_id, "
        << "clip_creator_twitch_id, clip_creator_login, vod_id, vod_offset, twitch_created_at) VALUES (MOD(CRC32('"
        << esc(clip.twitch_clip_id) << "'),32),'" << esc(clip.twitch_clip_id) << "','" << esc(clip.title) << "','" << esc(fp) << "','"
        << esc(clip.thumbnail_url) << "','" << esc(clip.embed_url) << "'," << clip.duration << ","
        << clip.view_count << ",'" << esc(clip.broadcaster_name) << "','" << esc(clip.broadcaster_id) << "','"
        << esc(clip.broadcaster_avatar) << "','" << esc(clip.broadcaster_lang) << "','"
        << esc(clip.broadcaster_login) << "'," << (clip.is_mature ? 1 : 0) << ",'"
        << esc(clip.classification_source) << "'," << (clip.game_id ? std::to_string(clip.game_id) : "NULL")
        << "," << (clip.author_id ? std::to_string(clip.author_id) : "NULL") << ",'"
        << esc(clip.creator_twitch_id) << "','" << esc(clip.creator_login) << "',"
        << (clip.vod_id.empty() ? "NULL" : "'" + esc(clip.vod_id) + "'") << "," << clip.vod_offset << ","
        << (clip.twitch_created_at.empty() ? "NULL" : "'" + esc(clip.twitch_created_at) + "'")
        << ") ON DUPLICATE KEY UPDATE "
        << "id=LAST_INSERT_ID(id), "
        << "discovery_bucket=VALUES(discovery_bucket), "
        << "title=VALUES(title), "
        << "title_fingerprint=VALUES(title_fingerprint), "
        << "thumbnail_url=VALUES(thumbnail_url), "
        << "embed_url=VALUES(embed_url), "
        << "duration=VALUES(duration), "
        << "view_count=VALUES(view_count), "
        << "broadcaster_name=VALUES(broadcaster_name), broadcaster_login=VALUES(broadcaster_login), "
           "is_mature=IF(clips.classification_source='manual',clips.is_mature,VALUES(is_mature)), "
           "classification_source=IF(clips.classification_source='manual',clips.classification_source,VALUES("
           "classification_source)), "
        << "broadcaster_id=CASE WHEN VALUES(broadcaster_id)='' THEN clips.broadcaster_id ELSE "
           "VALUES(broadcaster_id) END, "
        << "broadcaster_avatar=CASE WHEN VALUES(broadcaster_avatar)='' THEN clips.broadcaster_avatar ELSE "
           "VALUES(broadcaster_avatar) END, "
        << "game_id=COALESCE(VALUES(game_id), clips.game_id), "
        << "vod_id=CASE WHEN VALUES(vod_id) IS NULL OR VALUES(vod_id)='' THEN clips.vod_id ELSE "
           "VALUES(vod_id) END, "
        << "vod_offset=CASE WHEN VALUES(vod_offset)=0 THEN clips.vod_offset ELSE VALUES(vod_offset) END, "
        << "twitch_created_at=CASE WHEN VALUES(twitch_created_at) IS NULL OR VALUES(twitch_created_at)='' "
        << "THEN clips.twitch_created_at ELSE VALUES(twitch_created_at) END, "
        << "broadcaster_lang=CASE WHEN VALUES(broadcaster_lang)='' THEN clips.broadcaster_lang ELSE "
           "VALUES(broadcaster_lang) END, "
        << "clip_creator_twitch_id=CASE WHEN VALUES(clip_creator_twitch_id)='' THEN "
           "clips.clip_creator_twitch_id ELSE VALUES(clip_creator_twitch_id) END, "
        << "clip_creator_login=CASE WHEN VALUES(clip_creator_login)='' THEN clips.clip_creator_login ELSE "
           "VALUES(clip_creator_login) END";

    if (database::query(conn, sql.str().c_str()))
        throw std::runtime_error(std::string("Create clip failed: ") + mysql_error(conn));

    affected = mysql_affected_rows(conn);
    return mysql_insert_id(conn);
}

bool ClipRepository::update(const models::Clip &clip) {
    database::ScopedConnection conn(pool_);
    std::string sql = "UPDATE clips SET title='" + database::escape(conn.raw(), clip.title) +
                      "', thumbnail_url='" + database::escape(conn.raw(), clip.thumbnail_url) +
                      "' WHERE id=" + std::to_string(clip.id);
    return database::query(conn.raw(), sql.c_str()) == 0;
}

bool ClipRepository::remove(uint64_t id) {
    database::ScopedConnection conn(pool_);
    return database::query(conn.raw(), ("DELETE FROM clips WHERE id=" + std::to_string(id)).c_str()) == 0;
}

bool ClipRepository::record_user_view(uint64_t user_id, uint64_t clip_id) {
    return database::retry_transaction([&] {
    database::ScopedConnection conn(pool_);
    database::Transaction tx(conn.raw());
    std::string sql = "INSERT INTO view_history (user_id, clip_id, viewed_at) VALUES (" +
                      std::to_string(user_id) + "," + std::to_string(clip_id) + ", UTC_TIMESTAMP())";
    if (database::query(conn.raw(), sql.c_str()) != 0)
        return false;

    const auto activity = "INSERT INTO recommendation_activity(user_id,clip_id) VALUES(" +
        std::to_string(user_id) + "," + std::to_string(clip_id) +
        ") ON DUPLICATE KEY UPDATE last_opened_at=IF(last_opened_at<UTC_TIMESTAMP()-INTERVAL 30 MINUTE,"
        "UTC_TIMESTAMP(),last_opened_at)";
    database::query(conn.raw(), activity.c_str());
    if(mysql_affected_rows(conn.raw())>0)
        mark_recommendation_dirty(conn.raw(),user_id,clip_id);
    tx.commit();
    return true;
    });
}

bool ClipRepository::upsert_clip_engagement(uint64_t user_id, uint64_t clip_id, float watched_sec,
                                            float duration_sec, bool session_final) {
    database::ScopedConnection conn(pool_);
    const float dur = std::max(duration_sec, 1.0f);
    const float comp = std::min(1.0f, watched_sec / dur);
    const int wsec = static_cast<int>(std::max(0.0f, watched_sec));
    const int pos_sec = wsec;
    const bool early = session_final && duration_sec > 10.0f && (comp < 0.25f || watched_sec < 5.0f);
    const int early_i = early ? 1 : 0;

    std::ostringstream comp_s;
    comp_s << std::fixed << std::setprecision(6) << comp;

    std::ostringstream sql;
    sql << "INSERT INTO user_clip_engagement (user_id, clip_id, watch_time_sec, max_position_sec, "
           "completion_ratio, early_skip) VALUES ("
        << user_id << "," << clip_id << "," << wsec << "," << pos_sec << "," << comp_s.str() << "," << early_i
        << ") "
        << "ON DUPLICATE KEY UPDATE "
        << "watch_time_sec=GREATEST(watch_time_sec, VALUES(watch_time_sec)), "
        << "max_position_sec=GREATEST(max_position_sec, VALUES(max_position_sec)), "
        << "completion_ratio=GREATEST(completion_ratio, VALUES(completion_ratio)), "
        << "early_skip=early_skip OR VALUES(early_skip)";
    return database::query(conn.raw(), sql.str().c_str()) == 0;
}

bool ClipRepository::load_feed_mutes(uint64_t user_id, FeedMuteSet &out) {
    out = {};
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT target_key FROM user_feed_blocks WHERE user_id=" + std::to_string(user_id);
    if (database::query(conn.raw(), sql.c_str()))
        return false;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return false;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        if (!row[0])
            continue;
        std::string key = row[0];
        if (key.rfind("clip:", 0) == 0) {
            try {
                out.exclude_clip_ids.push_back(std::stoull(key.substr(5)));
            } catch (...) {
            }
        } else if (key.rfind("game:", 0) == 0) {
            try {
                out.mute_game_ids.push_back(std::stoull(key.substr(5)));
            } catch (...) {
            }
        } else if (key.rfind("bc:", 0) == 0) {
            out.mute_broadcaster_ids.push_back(key.substr(3));
        } else if (key.rfind("bl:", 0) == 0) {
            out.mute_broadcaster_logins.push_back(key.substr(3));
        }
    }
    res.reset();

    std::string sql2 = "SELECT mute_keywords, clip_languages FROM user_settings WHERE user_id=" +
                       std::to_string(user_id) + " LIMIT 1";
    if (database::query(conn.raw(), sql2.c_str()) == 0) {
        database::Result res2(mysql_store_result(conn.raw()));
        if (res2) {
            MYSQL_ROW row2 = mysql_fetch_row(res2);
            if (row2) {
                if (row2[0]) {
                    try {
                        auto j = nlohmann::json::parse(row2[0]);
                        if (j.is_array()) {
                            for (const auto &item : j) {
                                if (!item.is_string())
                                    continue;
                                std::string s = item.get<std::string>();
                                if (s.size() > 64)
                                    s = utils::utf8_prefix(s, 64);
                                if (!s.empty())
                                    out.mute_title_keywords.push_back(std::move(s));
                            }
                        }
                    } catch (...) {
                    }
                }
                if (row2[1]) {
                    try {
                        auto j = nlohmann::json::parse(row2[1]);
                        if (j.is_array() && !j.empty()) {
                            for (const auto &item : j) {
                                if (!item.is_string())
                                    continue;
                                std::string s = item.get<std::string>();
                                std::string norm;
                                for (char ch : s) {
                                    if (ch >= 'A' && ch <= 'Z')
                                        norm += static_cast<char>(ch - 'A' + 'a');
                                    else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                                             ch == '-')
                                        norm += ch;
                                    if (norm.size() >= 16)
                                        break;
                                }
                                if (!norm.empty())
                                    out.only_broadcast_langs.push_back(std::move(norm));
                                if (out.only_broadcast_langs.size() >= 24)
                                    break;
                            }
                        }
                    } catch (...) {
                    }
                }
            }
            res2.reset();
        }
    }
    return true;
}

bool ClipRepository::add_feed_block(uint64_t user_id, const std::string &target_key) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "INSERT IGNORE INTO user_feed_blocks (user_id, target_key) VALUES (" +
                      std::to_string(user_id) + ",'" + esc(target_key) + "')";
    database::query(conn.raw(), sql.c_str());
    if(target_key.starts_with("clip:")) {
        const auto id=std::stoull(target_key.substr(5));
        database::query(conn.raw(),("INSERT INTO recommendation_dirty_clips(clip_id) SELECT id FROM clips WHERE id="+
            std::to_string(id)+" ON DUPLICATE KEY UPDATE generation=generation+1").c_str());
    }
    return true;
}

LikeUpsertResult ClipRepository::upsert_user_like(uint64_t user_id, uint64_t clip_id, bool want_like) {
    return database::retry_transaction([&]() -> LikeUpsertResult {
        if (user_id == 0)
            return {};
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        auto clipq =
            "SELECT author_id,clip_creator_twitch_id FROM clips WHERE id=" + std::to_string(clip_id) +
            " AND unavailable_at IS NULL FOR UPDATE";
        database::query(c, clipq.c_str());
        database::Result clip(mysql_store_result(c));
        auto cliprow = mysql_fetch_row(clip);
        if (!cliprow)
            return {};
        uint64_t owner = cliprow[0] ? std::stoull(cliprow[0]) : 0;
        std::string creator = cliprow[1] ? cliprow[1] : "";
        clip.reset();
        if (owner == 0 && !creator.empty()) {
            auto lookup = "SELECT id FROM users WHERE twitch_id='" + database::escape(c, creator) + "'";
            database::query(c, lookup.c_str());
            database::Result result(mysql_store_result(c));
            auto r = mysql_fetch_row(result);
            if (r && r[0])
                owner = std::stoull(r[0]);
        }
        auto lock_users = "SELECT id FROM users WHERE id IN(" + std::to_string(user_id) + "," +
                          std::to_string(owner) + ") ORDER BY id FOR UPDATE";
        database::query(c, lock_users.c_str());
        database::Result locks(mysql_store_result(c));
        locks.reset();
        auto q = "SELECT is_like FROM likes WHERE user_id=" + std::to_string(user_id) +
                 " AND clip_id=" + std::to_string(clip_id);
        database::query(c, q.c_str());
        database::Result vote(mysql_store_result(c));
        auto row = mysql_fetch_row(vote);
        bool exists = row != nullptr;
        bool previous = exists && row[0] && std::string(row[0]) == "1";
        vote.reset();
        const bool remove = exists && previous == want_like;
        int likes = (remove ? 0 : (want_like ? 1 : 0)) - (exists && previous ? 1 : 0);
        int dislikes = (remove ? 0 : (want_like ? 0 : 1)) - (exists && !previous ? 1 : 0);
        std::string mutation;
        if (remove)
            mutation = "DELETE FROM likes WHERE user_id=" + std::to_string(user_id) +
                       " AND clip_id=" + std::to_string(clip_id);
        else
            mutation = "INSERT INTO likes(user_id,clip_id,is_like) VALUES(" + std::to_string(user_id) + "," +
                       std::to_string(clip_id) + "," + (want_like ? "1" : "0") +
                       ") ON DUPLICATE KEY UPDATE is_like=VALUES(is_like),created_at=UTC_TIMESTAMP()";
        database::query(c, mutation.c_str());
        auto count = "UPDATE clips SET like_count=GREATEST(0,like_count+(" + std::to_string(likes) +
                     ")),dislike_count=GREATEST(0,dislike_count+(" + std::to_string(dislikes) +
                     ")) WHERE id=" + std::to_string(clip_id);
        database::query(c, count.c_str());
        mark_recommendation_dirty(c,user_id,clip_id);
        if (owner == 0 && !creator.empty()) {
            auto lookup = "SELECT id FROM users WHERE twitch_id='" + database::escape(c, creator) + "'";
            database::query(c, lookup.c_str());
            database::Result result(mysql_store_result(c));
            auto r = mysql_fetch_row(result);
            if (r && r[0])
                owner = std::stoull(r[0]);
        }
        if (owner && owner != user_id) {
            auto reputation = "UPDATE users SET reputation=GREATEST(0,reputation+(" +
                              std::to_string(likes * 2 - dislikes) + ")) WHERE id=" + std::to_string(owner);
            database::query(c, reputation.c_str());
        }
        tx.commit();
        return {true, 0};
    });
}

std::optional<bool> ClipRepository::get_user_vote(uint64_t user_id, uint64_t clip_id) {
    database::ScopedConnection conn(pool_);
    std::string q = "SELECT is_like FROM likes WHERE user_id=" + std::to_string(user_id) +
                    " AND clip_id=" + std::to_string(clip_id) + " LIMIT 1";
    if (database::query(conn.raw(), q.c_str()) != 0)
        return std::nullopt;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return std::nullopt;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row || !row[0]) {
        res.reset();
        return std::nullopt;
    }
    const bool like = std::atoi(row[0]) != 0;
    res.reset();
    return like;
}

bool ClipRepository::insert_content_report(uint64_t reporter_id, const std::string &target_type,
                                           uint64_t target_id, const std::string &reason) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "INSERT INTO content_reports (reporter_id, target_type, target_id, reason) VALUES (" +
                      std::to_string(reporter_id) + ",'" + esc(target_type) + "'," +
                      std::to_string(target_id) + ",'" + esc(reason) + "')";
    return database::query(conn.raw(), sql.c_str()) == 0;
}

std::vector<nlohmann::json> ClipRepository::list_content_reports(uint32_t limit, uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT r.id,r.reporter_id,r.target_type,r.target_id,r.reason,r.status,r.created_at,"
                      "u.username,c.clip_id,c.user_id,c.body,cl.title "
                      "FROM content_reports r LEFT JOIN users u ON u.id=r.reporter_id "
                      "LEFT JOIN comments c ON r.target_type='comment' AND c.id=r.target_id "
                      "LEFT JOIN clips cl ON r.target_type='clip' AND cl.id=r.target_id "
                      "WHERE r.status='open' ORDER BY r.created_at DESC,r.id DESC LIMIT " +
                      std::to_string(limit) + " OFFSET " + std::to_string(offset);
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<nlohmann::json> out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        nlohmann::json j = {
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"reporter_id", row[1] ? std::stoull(row[1]) : 0ULL},
            {"target_type", row[2] ? std::string(row[2]) : ""},
            {"target_id", row[3] ? std::stoull(row[3]) : 0ULL},
            {"reason", row[4] ? std::string(row[4]) : ""},
            {"status", row[5] ? std::string(row[5]) : ""},
            {"created_at", row[6] ? std::string(row[6]) : ""},
            {"reporter_username", row[7] ? row[7] : ""},
            {"clip_id", row[8] ? nlohmann::json(std::stoull(row[8])) : nlohmann::json(nullptr)},
            {"subject_user_id", row[9] ? nlohmann::json(std::stoull(row[9])) : nlohmann::json(nullptr)},
            {"comment_content", row[10] ? row[10] : ""},
            {"clip_title", row[11] ? row[11] : ""},
        };
        out.push_back(std::move(j));
    }
    res.reset();
    return out;
}

bool ClipRepository::mark_clip_unavailable(uint64_t clip_id, const std::string &reason,
                                           uint64_t actor_user_id) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "UPDATE clips SET unavailable_at=UTC_TIMESTAMP(), removed_reason='" + esc(reason) +
                      "' WHERE id=" + std::to_string(clip_id);
    if (database::query(conn.raw(), sql.c_str()) != 0)
        return false;
    nlohmann::json details = {{"reason", reason}};
    conn.release();
    log_compliance("clip_unavailable", clip_id, actor_user_id, details);
    return true;
}

void ClipRepository::log_compliance(const std::string &action, uint64_t clip_id, uint64_t actor_user_id,
                                    const nlohmann::json &details) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string dj = esc(details.dump());
    std::string sql = "INSERT INTO compliance_log (action, clip_id, actor_user_id, details_json) VALUES ('" +
                      esc(action) + "'," + (clip_id ? std::to_string(clip_id) : "NULL") + "," +
                      (actor_user_id ? std::to_string(actor_user_id) : "NULL") + ",'" + dj + "')";
    database::query(conn.raw(), sql.c_str());
}

std::vector<nlohmann::json> ClipRepository::get_user_recent_view_activity(uint64_t user_id, uint32_t limit,
                                                                          uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::ostringstream sql;
    sql << "SELECT vh.viewed_at, c.id, c.title, c.thumbnail_url, c.broadcaster_name, c.twitch_clip_id "
        << "FROM view_history vh "
        << "INNER JOIN clips c ON c.id = vh.clip_id AND c.unavailable_at IS NULL AND c.moderation_hidden=0 "
        << "WHERE vh.user_id = " << user_id << " ORDER BY vh.viewed_at DESC LIMIT " << limit << " OFFSET "
        << offset;
    if (database::query(conn.raw(), sql.str().c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<nlohmann::json> out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        out.push_back(nlohmann::json{
            {"viewed_at", row[0] ? std::string(row[0]) : ""},
            {"clip_id", row[1] ? std::stoull(row[1]) : 0ULL},
            {"title", row[2] ? std::string(row[2]) : ""},
            {"thumbnail_url", row[3] ? std::string(row[3]) : ""},
            {"broadcaster_name", row[4] ? std::string(row[4]) : ""},
            {"twitch_clip_id", row[5] ? std::string(row[5]) : ""},
        });
    }
    res.reset();
    return out;
}

std::vector<uint64_t> ClipRepository::get_recent_viewed_clip_ids(uint64_t user_id, uint32_t limit) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT clip_id FROM view_history WHERE user_id=" + std::to_string(user_id) +
                      " ORDER BY viewed_at DESC LIMIT " + std::to_string(limit);
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<uint64_t> ids;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        if (row[0])
            ids.push_back(std::stoull(row[0]));
    }
    res.reset();
    return ids;
}

std::unordered_set<uint64_t> ClipRepository::clip_ids_seen_in_history(uint64_t user_id,
                                                                      const std::vector<uint64_t> &clip_ids) {
    std::unordered_set<uint64_t> out;
    if (user_id == 0 || clip_ids.empty())
        return out;
    database::ScopedConnection conn(pool_);
    constexpr size_t kChunk = 450;
    for (size_t off = 0; off < clip_ids.size(); off += kChunk) {
        const size_t n = std::min(kChunk, clip_ids.size() - off);
        std::ostringstream sql;
        sql << "SELECT clip_id FROM view_history WHERE user_id=" << user_id << " AND clip_id IN (";
        for (size_t i = 0; i < n; ++i) {
            if (i)
                sql << ",";
            sql << clip_ids[off + i];
        }
        sql << ")";
        if (database::query(conn.raw(), sql.str().c_str()) != 0)
            continue;
        database::Result res(mysql_store_result(conn.raw()));
        if (!res)
            continue;
        while (MYSQL_ROW row = mysql_fetch_row(res)) {
            if (row[0])
                out.insert(std::stoull(row[0]));
        }
        res.reset();
    }
    return out;
}

std::vector<uint64_t> ClipRepository::get_recent_active_user_ids(uint32_t days_back, uint32_t limit) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT DISTINCT user_id FROM view_history "
                      "WHERE viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL " +
                      std::to_string(days_back) +
                      " DAY) "
                      "ORDER BY user_id DESC LIMIT " +
                      std::to_string(limit);
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<uint64_t> out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        if (row[0])
            out.push_back(std::stoull(row[0]));
    }
    res.reset();
    return out;
}

std::vector<uint64_t> ClipRepository::get_top_game_ids_by_user(uint64_t user_id, uint32_t limit) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT c.game_id "
                      "FROM view_history vh "
                      "JOIN clips c ON c.id=vh.clip_id "
                      "WHERE vh.user_id=" +
                      std::to_string(user_id) +
                      " AND c.game_id IS NOT NULL "
                      "GROUP BY c.game_id ORDER BY COUNT(*) DESC LIMIT " +
                      std::to_string(limit);
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<uint64_t> ids;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        if (row[0])
            ids.push_back(std::stoull(row[0]));
    }
    res.reset();
    return ids;
}

std::vector<std::string> ClipRepository::get_top_broadcasters_by_user(uint64_t user_id, uint32_t limit) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT c.broadcaster_id "
                      "FROM view_history vh "
                      "JOIN clips c ON c.id=vh.clip_id "
                      "WHERE vh.user_id=" +
                      std::to_string(user_id) +
                      " AND c.broadcaster_id<>'' "
                      "GROUP BY c.broadcaster_id ORDER BY COUNT(*) DESC LIMIT " +
                      std::to_string(limit);
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<std::string> ids;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        if (row[0])
            ids.emplace_back(row[0]);
    }
    res.reset();
    return ids;
}

std::vector<std::string> ClipRepository::get_similar_broadcasters(uint64_t user_id, uint32_t limit) {
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT c2.broadcaster_id "
        "FROM view_history v_me "
        "JOIN clips c_me ON c_me.id=v_me.clip_id "
        "JOIN view_history v_other ON v_other.clip_id=v_me.clip_id AND v_other.user_id<>v_me.user_id "
        "JOIN view_history v_other_all ON v_other_all.user_id=v_other.user_id "
        "JOIN clips c2 ON c2.id=v_other_all.clip_id "
        "WHERE v_me.user_id=" +
        std::to_string(user_id) +
        " "
        "AND c2.broadcaster_id<>'' "
        "AND c2.broadcaster_id NOT IN ("
        "SELECT DISTINCT c_self.broadcaster_id "
        "FROM view_history v_self "
        "JOIN clips c_self ON c_self.id=v_self.clip_id "
        "WHERE v_self.user_id=" +
        std::to_string(user_id) +
        " AND c_self.broadcaster_id<>''"
        ") "
        "GROUP BY c2.broadcaster_id "
        "ORDER BY COUNT(*) DESC LIMIT " +
        std::to_string(limit);
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<std::string> out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        if (row[0])
            out.emplace_back(row[0]);
    }
    res.reset();
    return out;
}

std::vector<std::string> ClipRepository::get_network_broadcasters_from_site_follows(uint64_t viewer_id,
                                                                                    uint32_t limit) {
    if (viewer_id == 0 || limit == 0)
        return {};
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT COALESCE(NULLIF(pe.twitch_id,''),c.broadcaster_id) AS broadcaster_id,COUNT(*) n FROM "
        "profile_follows pf JOIN profile_entities pe ON pe.profile_key=pf.profile_key LEFT JOIN view_history "
        "vh ON vh.user_id=pe.local_user_id AND vh.viewed_at>=UTC_TIMESTAMP()-INTERVAL 14 DAY LEFT JOIN clips "
        "c ON c.id=vh.clip_id WHERE pf.follower_user_id=" +
        std::to_string(viewer_id) +
        " AND (pe.twitch_id IS NOT NULL OR c.broadcaster_id<>'') GROUP BY "
        "COALESCE(NULLIF(pe.twitch_id,''),c.broadcaster_id) ORDER BY n DESC "
        "LIMIT " +
        std::to_string(std::min(limit, 100u));
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<std::string> out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        if (row[0])
            out.emplace_back(row[0]);
    }
    res.reset();
    return out;
}

std::vector<std::string> ClipRepository::get_coview_broadcasters_for_taste(uint64_t user_id, uint32_t limit) {
    if (user_id == 0 || limit == 0)
        return {};
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT c2.broadcaster_id "
        "FROM view_history v_me "
        "JOIN clips c_me ON c_me.id=v_me.clip_id "
        "JOIN view_history v_other ON v_other.clip_id=v_me.clip_id AND v_other.user_id<>v_me.user_id "
        "JOIN view_history v_other_all ON v_other_all.user_id=v_other.user_id "
        "JOIN clips c2 ON c2.id=v_other_all.clip_id "
        "WHERE v_me.user_id=" +
        std::to_string(user_id) +
        " "
        "AND c2.broadcaster_id<>'' "
        "AND v_me.viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 14 DAY) "
        "AND v_other.viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 14 DAY) "
        "AND v_other_all.viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 14 DAY) "
        "AND c2.broadcaster_id NOT IN ("
        "SELECT DISTINCT c_self.broadcaster_id "
        "FROM view_history v_self "
        "JOIN clips c_self ON c_self.id=v_self.clip_id "
        "WHERE v_self.user_id=" +
        std::to_string(user_id) +
        " AND c_self.broadcaster_id<>''"
        ") "
        "GROUP BY c2.broadcaster_id "
        "ORDER BY COUNT(*) DESC LIMIT " +
        std::to_string(limit);
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<std::string> out;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        if (row[0])
            out.emplace_back(row[0]);
    }
    res.reset();
    return out;
}

std::vector<models::Clip> ClipRepository::find_home_explore(uint32_t limit,
                                                            const std::vector<uint64_t> &exclude_clip_ids,
                                                            const FeedMuteSet *mutes, bool with_tags) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    const char *twitch_dt = "STR_TO_DATE(c.twitch_created_at, '%Y-%m-%dT%H:%i:%sZ')";

    std::ostringstream sql;
    sql << "SELECT c.*, g.name AS game_name FROM clips c "
        << "LEFT JOIN games g ON c.game_id=g.id "
        << "LEFT JOIN " << promo_boost_subquery("'all','explore','feed'") << " ON promo.clip_id=c.id "
        << "WHERE ("
        << "(c.twitch_created_ts IS NOT NULL AND c.twitch_created_ts >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL "
           "14 DAY)) "
        << "OR (c.twitch_created_ts IS NULL AND " << twitch_dt
        << " >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 14 DAY)) "
        << ") AND c.view_count >= 1 ";
    if (!exclude_clip_ids.empty()) {
        sql << "AND c.id NOT IN (";
        append_in_numeric(sql, exclude_clip_ids);
        sql << ") ";
    }
    append_public_feed_filters(sql, mutes, esc);
    sql << "ORDER BY (COALESCE(promo.promo_boost,0) * 12.0 + LEAST(400.0, c.view_count) * 0.08 + RAND() * "
           "95.0) DESC "
        << "LIMIT " << limit;

    if (database::query(conn.raw(), sql.str().c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<models::Clip> clips;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        auto c = row_to_clip(row, res);

        clips.push_back(std::move(c));
    }
    res.reset();
    conn.release();
    std::vector<models::Clip *> tag_targets;
    for (auto &clip : clips)
        tag_targets.push_back(&clip);
    if (with_tags)
        fill_tags_bulk(tag_targets);
    return clips;
}

bool ClipRepository::upsert_user_recommendations(uint64_t user_id,
                                                 const std::vector<std::string> &broadcaster_ids) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &id : broadcaster_ids)
        arr.push_back(id);
    const auto payload = esc(arr.dump());
    std::string sql =
        "INSERT INTO user_recommendations (user_id, recommended_broadcasters_json, calculated_at) VALUES (" +
        std::to_string(user_id) + ", '" + payload +
        "', UTC_TIMESTAMP()) "
        "ON DUPLICATE KEY UPDATE "
        "recommended_broadcasters_json=VALUES(recommended_broadcasters_json), "
        "calculated_at=UTC_TIMESTAMP()";
    return database::query(conn.raw(), sql.c_str()) == 0;
}

std::vector<std::string> ClipRepository::get_user_recommendations(uint64_t user_id, uint32_t max_age_hours,
                                                                  uint32_t limit) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT recommended_broadcasters_json "
                      "FROM user_recommendations "
                      "WHERE user_id=" +
                      std::to_string(user_id) + " AND calculated_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL " +
                      std::to_string(max_age_hours) +
                      " HOUR) "
                      "LIMIT 1";
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row || !row[0]) {
        res.reset();
        return {};
    }
    std::vector<std::string> out;
    try {
        auto arr = nlohmann::json::parse(row[0]);
        if (arr.is_array()) {
            for (const auto &item : arr) {
                if (!item.is_string())
                    continue;
                out.push_back(item.get<std::string>());
                if (out.size() >= limit)
                    break;
            }
        }
    } catch (...) {
    }
    res.reset();
    return out;
}

std::vector<nlohmann::json>
ClipRepository::get_broadcaster_cards(const std::vector<std::string> &broadcaster_ids, uint32_t limit) {
    if (broadcaster_ids.empty())
        return {};
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::ostringstream sql;
    sql << "SELECT c.broadcaster_id, "
        << "MAX(c.broadcaster_name) AS broadcaster_name, "
        << "MAX(c.broadcaster_avatar) AS broadcaster_avatar, "
        << "MAX(g.name) AS game_name, "
        << "COUNT(*) AS clips_count, "
        << "SUM(c.view_count) AS views_sum "
        << "FROM clips c LEFT JOIN games g ON g.id=c.game_id "
        << "WHERE c.broadcaster_id IN (";
    append_in_quoted(sql, broadcaster_ids, esc);
    sql << ") GROUP BY c.broadcaster_id "
        << "ORDER BY FIELD(c.broadcaster_id, ";
    append_in_quoted(sql, broadcaster_ids, esc);
    sql << ") LIMIT " << limit;
    if (database::query(conn.raw(), sql.str().c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<nlohmann::json> cards;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        nlohmann::json item = {{"broadcaster_id", row[0] ? std::string(row[0]) : ""},
                               {"broadcaster_name", row[1] ? std::string(row[1]) : ""},
                               {"broadcaster_avatar", row[2] ? std::string(row[2]) : ""},
                               {"game_name", row[3] ? std::string(row[3]) : ""},
                               {"clips_count", row[4] ? std::stoi(row[4]) : 0},
                               {"views_sum", row[5] ? std::stoll(row[5]) : 0}};
        cards.push_back(std::move(item));
    }
    res.reset();
    return cards;
}

std::optional<std::string> ClipRepository::get_app_setting(const std::string &setting_key) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql =
        "SELECT setting_value FROM app_settings WHERE setting_key='" + esc(setting_key) + "' LIMIT 1";
    if (database::query(conn.raw(), sql.c_str()))
        return std::nullopt;
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return std::nullopt;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row || !row[0]) {
        res.reset();
        return std::nullopt;
    }
    std::string value = row[0];
    res.reset();
    return value;
}

bool ClipRepository::set_app_setting(const std::string &setting_key, const std::string &setting_value) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql =
        "INSERT INTO app_settings (setting_key, setting_value, updated_at) VALUES ('" + esc(setting_key) +
        "','" + esc(setting_value) +
        "', UTC_TIMESTAMP()) "
        "ON DUPLICATE KEY UPDATE setting_value=VALUES(setting_value), updated_at=UTC_TIMESTAMP()";
    return database::query(conn.raw(), sql.c_str()) == 0;
}

std::vector<nlohmann::json> ClipRepository::list_clip_promotions(uint32_t limit, uint32_t offset) {
    database::ScopedConnection conn(pool_);
    std::string sql =
        "SELECT p.id, p.clip_id, p.boost_score, p.placement, p.label, p.starts_at, p.ends_at, p.created_at, "
        "c.title, c.broadcaster_name "
        "FROM clip_promotions p "
        "LEFT JOIN clips c ON c.id = p.clip_id "
        "ORDER BY p.id DESC LIMIT " +
        std::to_string(limit) + " OFFSET " + std::to_string(offset);
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};
    std::vector<nlohmann::json> rows;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        rows.push_back(nlohmann::json{
            {"id", row[0] ? std::stoull(row[0]) : 0ULL},
            {"clip_id", row[1] ? std::stoull(row[1]) : 0ULL},
            {"boost_score", row[2] ? std::stod(row[2]) : 0.0},
            {"placement", row[3] ? std::string(row[3]) : ""},
            {"label", row[4] ? std::string(row[4]) : ""},
            {"starts_at", row[5] ? std::string(row[5]) : ""},
            {"ends_at", row[6] ? std::string(row[6]) : ""},
            {"created_at", row[7] ? std::string(row[7]) : ""},
            {"clip_title", row[8] ? std::string(row[8]) : ""},
            {"broadcaster_name", row[9] ? std::string(row[9]) : ""},
        });
    }
    res.reset();
    return rows;
}

std::optional<uint64_t> ClipRepository::create_clip_promotion(uint64_t clip_id, double boost_score,
                                                              const std::string &placement,
                                                              const std::string &label,
                                                              const std::string &starts_at_iso,
                                                              const std::string &ends_at_iso) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::ostringstream sql;
    sql << "INSERT INTO clip_promotions (clip_id, boost_score, placement, label, starts_at, ends_at) VALUES ("
        << clip_id << "," << boost_score << ",'" << esc(placement) << "','" << esc(label) << "',";
    if (starts_at_iso.empty())
        sql << "NULL";
    else
        sql << "'" << esc(starts_at_iso) << "'";
    sql << ",";
    if (ends_at_iso.empty())
        sql << "NULL";
    else
        sql << "'" << esc(ends_at_iso) << "'";
    sql << ")";
    if (database::query(conn.raw(), sql.str().c_str()) != 0)
        return std::nullopt;
    return static_cast<uint64_t>(mysql_insert_id(conn.raw()));
}

bool ClipRepository::delete_clip_promotion(uint64_t promo_id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "DELETE FROM clip_promotions WHERE id=" + std::to_string(promo_id);
    return database::query(conn.raw(), sql.c_str()) == 0 && mysql_affected_rows(conn.raw()) > 0;
}

bool ClipRepository::ping_db() {
    database::ScopedConnection conn(pool_);
    database::query(conn.raw(), "SELECT 1");
    database::Result result(mysql_store_result(conn.raw()));
    return result && mysql_fetch_row(result);
}

std::unordered_map<std::string, ExistingClipRef>
ClipRepository::find_existing_twitch_clip_refs(const std::vector<std::string> &twitch_clip_ids) {
    std::unordered_map<std::string, ExistingClipRef> out;
    if (twitch_clip_ids.empty())
        return out;

    constexpr size_t kChunk = 200;
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };

    for (size_t off = 0; off < twitch_clip_ids.size(); off += kChunk) {
        const size_t end = std::min(off + kChunk, twitch_clip_ids.size());
        std::ostringstream sql;
        sql << "SELECT id, twitch_clip_id, broadcaster_id, broadcaster_avatar FROM clips WHERE "
               "twitch_clip_id IN (";
        for (size_t i = off; i < end; ++i) {
            if (i > off)
                sql << ',';
            sql << "'" << esc(twitch_clip_ids[i]) << "'";
        }
        sql << ")";
        if (database::query(conn.raw(), sql.str().c_str()))
            continue;

        database::Result res(mysql_store_result(conn.raw()));
        if (!res)
            continue;

        while (MYSQL_ROW row = mysql_fetch_row(res)) {
            ExistingClipRef r;
            r.id = row[0] ? std::stoull(row[0]) : 0;
            std::string tid = row[1] ? row[1] : "";
            r.broadcaster_id = row[2] ? row[2] : "";
            r.broadcaster_avatar = row[3] ? row[3] : "";
            out[std::move(tid)] = std::move(r);
        }
        res.reset();
    }
    return out;
}

uint64_t ClipRepository::count_clips_for_broadcaster(const std::string &twitch_login,
                                                     const std::string &twitch_user_id) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "SELECT COUNT(*) FROM clips WHERE ";
    if (!twitch_user_id.empty())
        sql += "broadcaster_id='" + esc(twitch_user_id) + "' OR ";
    sql += "LOWER(broadcaster_name)=LOWER('" + esc(twitch_login) + "')";
    if (database::query(conn.raw(), sql.c_str()))
        return 0;

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    uint64_t n = 0;
    if (row && row[0]) {
        try {
            n = std::stoull(row[0]);
        } catch (...) {
        }
    }
    res.reset();
    return n;
}

std::optional<models::Clip> ClipRepository::find_by_twitch_id(const std::string &twitch_clip_id) {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT * FROM clips WHERE twitch_clip_id='" + twitch_clip_id + "'";
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

    auto clip = row_to_clip(row, res);
    res.reset();
    conn.release();
    clip.tags = load_tags(clip.id);
    return clip;
}

bool ClipRepository::update_broadcaster_avatar(uint64_t id, const std::string &avatar_url) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql =
        "UPDATE clips SET broadcaster_avatar='" + esc(avatar_url) + "' WHERE id=" + std::to_string(id);
    return database::query(conn.raw(), sql.c_str()) == 0;
}

bool ClipRepository::update_broadcaster_metadata(uint64_t id, const std::string &broadcaster_id,
                                                 const std::string &avatar_url) {
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::string sql = "UPDATE clips SET broadcaster_id='" + esc(broadcaster_id) + "', broadcaster_avatar='" +
                      esc(avatar_url) + "' WHERE id=" + std::to_string(id);
    return database::query(conn.raw(), sql.c_str()) == 0;
}

void ClipRepository::fill_card_counts(const std::vector<models::Clip *> &clips) {
    std::vector<uint64_t> ids;
    for (auto *clip : clips)
        if (clip)
            ids.push_back(clip->id);
    if (ids.empty())
        return;
    std::ostringstream sql;
    sql << "SELECT c.id,(SELECT COUNT(*) FROM comments cm WHERE cm.clip_id=c.id),"
           "(SELECT COUNT(*) FROM view_history vh WHERE vh.clip_id=c.id) "
           "FROM clips c WHERE c.id IN (";
    append_in_numeric(sql, ids);
    sql << ")";
    database::ScopedConnection conn(pool_);
    if (database::query(conn.raw(), sql.str().c_str()))
        return;
    database::Result result(mysql_store_result(conn.raw()));
    if (!result)
        return;
    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> counts;
    while (auto row = mysql_fetch_row(result))
        counts.emplace(std::stoull(row[0]), std::pair{std::stoull(row[1]), std::stoull(row[2])});
    for (auto *clip : clips)
        if (clip) {
            const auto count = counts.find(clip->id);
            if (count != counts.end()) {
                clip->comment_count = count->second.first;
                clip->platform_view_count = count->second.second;
            }
        }
}

void ClipRepository::fill_user_votes(const std::vector<models::Clip *> &clips, uint64_t user_id) {
    std::vector<uint64_t> ids;
    std::unordered_map<uint64_t, models::Clip *> targets;
    for (auto *clip : clips) {
        if (clip) {
            clip->user_vote.reset();
            ids.push_back(clip->id);
            targets.emplace(clip->id, clip);
        }
    }
    if (!user_id || ids.empty())
        return;
    std::ostringstream sql;
    sql << "SELECT c.id,l.is_like,c.like_count,c.dislike_count FROM clips c "
           "LEFT JOIN likes l ON l.clip_id=c.id AND l.user_id=" << user_id << " WHERE c.id IN (";
    append_in_numeric(sql, ids);
    sql << ")";
    database::ScopedConnection conn(pool_);
    if (database::query(conn.raw(), sql.str().c_str()))
        return;
    database::Result result(mysql_store_result(conn.raw()));
    if (!result)
        return;
    while (auto row = mysql_fetch_row(result)) {
        auto target = targets.find(std::stoull(row[0]));
        if (target == targets.end())
            continue;
        auto &clip = *target->second;
        if (row[1])
            clip.user_vote = std::string(row[1]) == "1";
        clip.like_count = std::stoi(row[2]);
        clip.dislike_count = std::stoi(row[3]);
    }
}

std::vector<nlohmann::json> ClipRepository::get_viewed_games(uint64_t user_id) {
    if (!user_id)
        return {};
    database::ScopedConnection conn(pool_);
    // Rank only the viewer's history; deterministic ties favour recent discoveries.
    const std::string sql =
        "SELECT g.id,g.name,g.box_art,COUNT(*) AS watched_count "
        "FROM view_history vh JOIN clips c ON c.id=vh.clip_id "
        "JOIN games g ON g.id=c.game_id WHERE vh.user_id=" + std::to_string(user_id) +
        " AND c.unavailable_at IS NULL AND c.moderation_hidden=0 "
        "GROUP BY g.id ORDER BY watched_count DESC,MAX(vh.viewed_at) DESC,g.id LIMIT 10";
    if (database::query(conn.raw(), sql.c_str()))
        return {};
    database::Result result(mysql_store_result(conn.raw()));
    if (!result)
        return {};
    std::vector<nlohmann::json> games;
    while (auto row = mysql_fetch_row(result)) {
        games.push_back({{"id", std::stoull(row[0])}, {"name", std::string(row[1])},
                         {"box_art", row[2] ? nlohmann::json(row[2]) : nlohmann::json(nullptr)},
                         {"watched_count", std::stoull(row[3])}});
    }
    return games;
}

std::vector<nlohmann::json> ClipRepository::get_games() {
    database::ScopedConnection conn(pool_);
    std::string sql = "SELECT g.id, g.twitch_id, g.name, g.box_art, COUNT(c.id) AS clip_count "
                      "FROM games g LEFT JOIN clips c ON g.id=c.game_id "
                      "GROUP BY g.id ORDER BY clip_count DESC";
    if (database::query(conn.raw(), sql.c_str()))
        return {};

    database::Result res(mysql_store_result(conn.raw()));
    if (!res)
        return {};

    std::vector<nlohmann::json> games;
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        nlohmann::json g = {{"id", std::stoull(row[0])},
                            {"twitch_id", std::string(row[1])},
                            {"name", std::string(row[2])},
                            {"clip_count", std::stoi(row[4])}};
        if (row[3])
            g["box_art"] = std::string(row[3]);
        else
            g["box_art"] = nullptr;
        games.push_back(std::move(g));
    }
    res.reset();
    return games;
}

void ClipRepository::record_analytics_engagement(uint64_t user_id, const std::string &session_id,
                                                 uint64_t clip_id, bool session_final,
                                                 float watched_sec_total) {
    return database::retry_transaction([&]() -> void {
        if (user_id == 0 || session_id.empty())
            return;
        database::ScopedConnection conn(pool_);
        auto c = conn.raw();
        database::Transaction tx(c);
        const auto sid = "'" + database::escape(c, session_id) + "'";
        const auto identity = "user_id=" + std::to_string(user_id) + " AND session_id=" + sid +
                              " AND clip_id=" + std::to_string(clip_id);
        auto insert = "INSERT IGNORE INTO analytics_clip_sessions(user_id,session_id,clip_id) VALUES(" +
                      std::to_string(user_id) + "," + sid + "," + std::to_string(clip_id) + ")";
        database::query(c, insert.c_str());
        auto select =
            "SELECT watched_sec,finalized FROM analytics_clip_sessions WHERE " + identity + " FOR UPDATE";
        database::query(c, select.c_str());
        database::Result result(mysql_store_result(c));
        auto row = mysql_fetch_row(result);
        if (!row)
            throw std::runtime_error("Analytics session missing");
        unsigned previous = std::stoul(row[0]);
        bool finalized = std::string(row[1]) == "1";
        result.reset();
        if (finalized) {
            tx.commit();
            return;
        }
        unsigned watched = static_cast<unsigned>(std::max(0.0f, watched_sec_total));
        unsigned delta = watched > previous ? watched - previous : 0;
        unsigned final = session_final ? 1 : 0;
        auto update = "UPDATE analytics_clip_sessions SET watched_sec=GREATEST(watched_sec," +
                      std::to_string(watched) + "),finalized=" + std::to_string(final) + " WHERE " + identity;
        database::query(c, update.c_str());
        auto values =
            std::to_string(user_id) + "," + sid + "," + std::to_string(delta) + "," + std::to_string(final);
        auto session =
            "INSERT INTO analytics_sessions(user_id,session_id,total_watch_sec,clips_viewed) VALUES(" +
            values +
            ") ON DUPLICATE KEY UPDATE "
            "total_watch_sec=total_watch_sec+VALUES(total_watch_sec),clips_viewed=clips_viewed+VALUES("
            "clips_viewed),last_seen_at=UTC_TIMESTAMP()";
        database::query(c, session.c_str());
        auto daily =
            "INSERT INTO analytics_user_daily(user_id,activity_date,watch_sec,clips_viewed) VALUES(" +
            std::to_string(user_id) + ",UTC_DATE()," + std::to_string(delta) + "," + std::to_string(final) +
            ") ON DUPLICATE KEY UPDATE "
            "watch_sec=watch_sec+VALUES(watch_sec),clips_viewed=clips_viewed+VALUES(clips_viewed)";
        database::query(c, daily.c_str());
        if (delta || final) {
            auto event =
                "INSERT INTO analytics_feed_events(user_id,session_id,event_type,clip_id,value) VALUES(" +
                std::to_string(user_id) + "," + sid + ",'" +
                (session_final ? "watch_session_end" : "watch_heartbeat") + "'," + std::to_string(clip_id) +
                "," + std::to_string(delta) + ")";
            database::query(c, event.c_str());
        }
        tx.commit();
    });
}

bool ClipRepository::refresh_feature_store_clips() {
    database::ScopedConnection conn(pool_);
    const std::string sql =
        "INSERT INTO feature_store_clip (clip_id, age_hours, velocity_score, er_like, freshness_decay) "
        "SELECT c.id, "
        "GREATEST(0.25, TIMESTAMPDIFF(HOUR, COALESCE(STR_TO_DATE(c.twitch_created_at, '%Y-%m-%dT%H:%i:%sZ'), "
        "c.created_at), UTC_TIMESTAMP())), "
        "c.view_count / GREATEST(0.25, TIMESTAMPDIFF(HOUR, COALESCE(STR_TO_DATE(c.twitch_created_at, "
        "'%Y-%m-%dT%H:%i:%sZ'), c.created_at), UTC_TIMESTAMP()) / 24.0), "
        "COALESCE(_plat_l.plat_pl,0) / GREATEST(1, COALESCE(_plat_v.plat_pv,0)), "
        "EXP(-GREATEST(0, TIMESTAMPDIFF(HOUR, COALESCE(STR_TO_DATE(c.twitch_created_at, "
        "'%Y-%m-%dT%H:%i:%sZ'), "
        "c.created_at), UTC_TIMESTAMP())) / 168.0) "
        "FROM clips c "
        "LEFT JOIN (SELECT clip_id, COUNT(*) AS plat_pv FROM view_history GROUP BY clip_id) _plat_v ON "
        "_plat_v.clip_id=c.id "
        "LEFT JOIN (SELECT clip_id, SUM(CASE WHEN is_like=1 THEN 1 ELSE 0 END) AS plat_pl FROM likes GROUP "
        "BY clip_id) "
        "_plat_l ON _plat_l.clip_id=c.id "
        "ON DUPLICATE KEY UPDATE "
        "age_hours=VALUES(age_hours), velocity_score=VALUES(velocity_score), er_like=VALUES(er_like), "
        "freshness_decay=VALUES(freshness_decay), updated_at=UTC_TIMESTAMP()";
    return database::query(conn.raw(), sql.c_str()) == 0;
}

bool ClipRepository::refresh_feature_store_users() {
    database::ScopedConnection conn(pool_);
    // MySQL 8+: топ-5 игр по просмотрам за 7 дней и энтропия распределения по играм (нат. логарифм)
    const std::string sql =
        "INSERT INTO feature_store_user (user_id, watch_sec_7d, clips_viewed_7d, top_games_json, "
        "affinity_entropy) "
        "SELECT s.user_id, s.watch_sec, s.clips_viewed, tj.top_json, ent.affinity_entropy "
        "FROM ( "
        "  SELECT vh.user_id, "
        "    COALESCE(SUM(uce.watch_time_sec), 0) AS watch_sec, "
        "    COUNT(DISTINCT vh.clip_id) AS clips_viewed "
        "  FROM view_history vh "
        "  LEFT JOIN user_clip_engagement uce ON uce.user_id=vh.user_id AND uce.clip_id=vh.clip_id "
        "  WHERE vh.viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 7 DAY) "
        "  GROUP BY vh.user_id "
        ") s "
        "LEFT JOIN ( "
        "  SELECT ranked.user_id, JSON_ARRAYAGG(JSON_OBJECT('game_id', ranked.game_id, 'w', ranked.cnt)) AS "
        "top_json "
        "  FROM ( "
        "    SELECT gc.user_id, gc.game_id, gc.cnt, "
        "      ROW_NUMBER() OVER (PARTITION BY gc.user_id ORDER BY gc.cnt DESC) AS rn "
        "    FROM ( "
        "      SELECT vh.user_id, c.game_id, COUNT(*) AS cnt "
        "      FROM view_history vh "
        "      INNER JOIN clips c ON c.id = vh.clip_id AND c.game_id IS NOT NULL "
        "      WHERE vh.viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 7 DAY) "
        "      GROUP BY vh.user_id, c.game_id "
        "    ) gc "
        "  ) ranked "
        "  WHERE ranked.rn <= 5 "
        "  GROUP BY ranked.user_id "
        ") tj ON tj.user_id = s.user_id "
        "LEFT JOIN ( "
        "  SELECT z.user_id, -SUM((z.cnt / z.total) * LN(z.cnt / z.total)) AS affinity_entropy "
        "  FROM ( "
        "    SELECT gc2.user_id, gc2.game_id, gc2.cnt, "
        "      SUM(gc2.cnt) OVER (PARTITION BY gc2.user_id) AS total "
        "    FROM ( "
        "      SELECT vh.user_id, c.game_id, COUNT(*) AS cnt "
        "      FROM view_history vh "
        "      INNER JOIN clips c ON c.id = vh.clip_id AND c.game_id IS NOT NULL "
        "      WHERE vh.viewed_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 7 DAY) "
        "      GROUP BY vh.user_id, c.game_id "
        "    ) gc2 "
        "  ) z "
        "  WHERE z.total > 0 "
        "  GROUP BY z.user_id "
        ") ent ON ent.user_id = s.user_id "
        "ON DUPLICATE KEY UPDATE "
        "watch_sec_7d=VALUES(watch_sec_7d), clips_viewed_7d=VALUES(clips_viewed_7d), "
        "top_games_json=VALUES(top_games_json), affinity_entropy=VALUES(affinity_entropy), "
        "updated_at=UTC_TIMESTAMP()";
    return database::query(conn.raw(), sql.c_str()) == 0;
}

void ClipRepository::record_home_impressions(uint64_t user_id, const std::string &session_id,
                                             const std::vector<models::Clip> &hot,
                                             const std::vector<models::Clip> &fresh,
                                             const std::vector<models::Clip> &recommended) {
    if (user_id == 0 || session_id.empty())
        return;
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };

    std::ostringstream sql;
    sql << "INSERT INTO analytics_feed_events (user_id, session_id, event_type, block_type, clip_id, value) "
           "VALUES ";
    bool first = true;
    auto emit = [&](const char *block, const std::vector<models::Clip> &clips) {
        for (size_t i = 0; i < clips.size(); ++i) {
            if (!first)
                sql << ",";
            first = false;
            sql << "(" << user_id << ",'" << esc(session_id) << "','home_impression','" << block << "',"
                << clips[i].id << "," << static_cast<double>(i) << ")";
        }
    };
    emit("hot", hot);
    emit("fresh", fresh);
    emit("recommended", recommended);
    if (first)
        return;
    database::query(conn.raw(), sql.str().c_str());
}

nlohmann::json ClipRepository::get_analytics_summary_24h() {
    using json = nlohmann::json;
    json out = json::object();
    database::ScopedConnection conn(pool_);

    auto run_events = [&]() {
        json events = json::object();
        const char *q = "SELECT event_type, COUNT(*) FROM analytics_feed_events "
                        "WHERE created_at >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL 24 HOUR) GROUP BY event_type";
        if (database::query(conn.raw(), q) != 0)
            return events;
        database::Result res(mysql_store_result(conn.raw()));
        if (!res)
            return events;
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            if (row[0] && row[1])
                events[row[0]] = std::stoull(row[1]);
        }
        res.reset();
        return events;
    };
    out["feed_events_24h"] = run_events();

    const char *q_sess = "SELECT COUNT(*) FROM analytics_sessions WHERE last_seen_at >= "
                         "DATE_SUB(UTC_TIMESTAMP(), INTERVAL 24 HOUR)";
    if (database::query(conn.raw(), q_sess) == 0) {
        database::Result res(mysql_store_result(conn.raw()));
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0])
                out["sessions_touched_24h"] = std::stoull(row[0]);
            res.reset();
        }
    }

    const char *q_daily =
        "SELECT COALESCE(SUM(watch_sec),0), COALESCE(SUM(clips_viewed),0) FROM analytics_user_daily "
        "WHERE activity_date >= DATE_SUB(CURDATE(), INTERVAL 7 DAY)";
    if (database::query(conn.raw(), q_daily) == 0) {
        database::Result res(mysql_store_result(conn.raw()));
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0] && row[1]) {
                out["user_daily_7d"] =
                    json{{"watch_sec", std::stoull(row[0])}, {"clips_viewed", std::stoull(row[1])}};
            }
            res.reset();
        }
    }

    return out;
}

} // namespace clipstream::repositories
