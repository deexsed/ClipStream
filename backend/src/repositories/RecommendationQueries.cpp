#include "database/SqlEscape.hpp"
#include "repositories/ClipFeedSql.hpp"
#include "repositories/ClipRepository.hpp"
#include "repositories/RecommendationWrites.hpp"
#include "services/RecommendationScoring.hpp"
#include <algorithm>
#include <array>
#include <sstream>

namespace clipstream::repositories {
using namespace feed_sql;
services::recommendations::Profile ClipRepository::recommendation_profile(uint64_t user_id) {
    services::recommendations::Profile out;
    if (!user_id)
        return out;
    database::ScopedConnection conn(pool_);
    const auto uid = std::to_string(user_id);
    // Limit each indexed source before merging. Explicit votes override weak
    // implicit actions.
    const auto q = "WITH signals AS ("
                   "(SELECT clip_id,0.3 weight,last_opened_at happened FROM "
                   "recommendation_activity WHERE user_id=" +
                   uid +
                   " ORDER BY last_opened_at DESC LIMIT 150) UNION ALL "
                   "(SELECT clip_id,IF(is_like,3.0,-4.0),created_at FROM likes WHERE "
                   "user_id=" +
                   uid +
                   " ORDER BY created_at DESC LIMIT 150) UNION ALL "
                   "(SELECT clip_id,0.5,MAX(created_at) FROM comments WHERE user_id=" +
                   uid +
                   " GROUP BY clip_id ORDER BY MAX(created_at) DESC LIMIT 50)) "
                   "SELECT c.*,g.name game_name,IF(l.user_id IS NOT "
                   "NULL,IF(l.is_like,3.0,-4.0),MAX(s.weight)) rw,"
                   "GREATEST(0,TIMESTAMPDIFF(HOUR,MAX(s.happened),UTC_TIMESTAMP())/24.0) rd "
                   "FROM signals s JOIN clips c ON c.id=s.clip_id LEFT JOIN games g ON "
                   "g.id=c.game_id "
                   "LEFT JOIN likes l ON l.clip_id=c.id AND l.user_id=" +
                   uid +
                   " WHERE c.unavailable_at IS NULL AND c.moderation_hidden=0 GROUP BY "
                   "c.id,l.user_id,l.is_like "
                   "ORDER BY MAX(s.happened) DESC LIMIT 200";
    database::query(conn.raw(), q.c_str());
    database::Result res(mysql_store_result(conn.raw()));
    MYSQL_ROW row;
    const auto fields = mysql_num_fields(res);
    while ((row = mysql_fetch_row(res)))
        out.interactions.push_back(
            {row_to_clip(row, res), std::stod(row[fields - 2]), std::stod(row[fields - 1])});
    res.reset();
    const auto follows = "SELECT pe.twitch_id FROM profile_follows pf JOIN profile_entities pe "
                         "ON pe.profile_key=pf.profile_key WHERE pf.follower_user_id=" +
                         uid + " AND pe.twitch_id IS NOT NULL ORDER BY pf.created_at DESC LIMIT 200";
    database::query(conn.raw(), follows.c_str());
    database::Result fr(mysql_store_result(conn.raw()));
    while ((row = mysql_fetch_row(fr)))
        if (row[0])
            out.followed_channels.emplace_back(row[0]);
    fr.reset();
    const auto twitch = "SELECT broadcaster_id FROM recommendation_twitch_follows WHERE "
                        "user_id=" +
                        uid + " AND refreshed_at>UTC_TIMESTAMP()-INTERVAL 7 DAY LIMIT 200";
    database::query(conn.raw(), twitch.c_str());
    database::Result tr(mysql_store_result(conn.raw()));
    while ((row = mysql_fetch_row(tr)))
        out.followed_channels.emplace_back(row[0]);
    return out;
}

std::vector<services::recommendations::Candidate>
ClipRepository::recommendation_candidates(uint64_t user_id, const FeedMuteSet *mutes,
                                          const std::vector<uint64_t> &exclude,
                                          const std::vector<uint64_t> &only_ids, bool rush) {
    using services::recommendations::Candidate;
    database::ScopedConnection conn(pool_);
    auto esc = [&](const std::string &s) { return database::escape(conn.raw(), s); };
    std::ostringstream filters;
    append_public_feed_filters(filters, mutes, esc);
    if (!exclude.empty()) {
        filters << " AND c.id NOT IN (";
        append_in_numeric(filters, exclude);
        filters << ")";
    }
    if (user_id)
        filters << " AND NOT EXISTS(SELECT 1 FROM likes d WHERE d.user_id=" << user_id
                << " AND d.clip_id=c.id AND d.is_like=0)";
    if (rush && user_id && only_ids.empty()) {
        // Cross-session frequency cap; home impressions do not consume Rush
        // exploration.
        filters << " AND NOT EXISTS(SELECT 1 FROM recommendation_deliveries d "
                   "WHERE d.user_id="
                << user_id
                << " AND d.clip_id=c.id AND d.surface='rush' AND "
                   "d.impressed_at>UTC_TIMESTAMP()-INTERVAL 3 DAY)";
    }
    const auto uid = std::to_string(user_id);
    const std::string age = "GREATEST(0,TIMESTAMPDIFF(HOUR,COALESCE(c.twitch_"
                            "created_ts,c.created_at),UTC_TIMESTAMP()))";
    std::ostringstream pools;
    if (!only_ids.empty()) {
        pools << "SELECT c.id FROM clips c WHERE c.id IN (";
        append_in_numeric(pools, only_ids);
        pools << ")" << filters.str();
    } else {
        auto pool = [&](const std::string &join, const std::string &where, const std::string &order, int n) {
            if (pools.tellp() > 0)
                pools << " UNION ";
            pools << "(SELECT c.id FROM clips c " << join << " WHERE 1=1 " << filters.str() << where
                  << " ORDER BY " << order << " LIMIT " << n << ")";
        };
        pool("", "", "c.view_count DESC,c.id DESC", 160);
        pool("", " AND " + age + ">=720", "c.view_count DESC,c.id DESC", 120);
        // Zero-view new clips are eligible. No minimum popularity threshold.
        pool("", " AND " + age + "<=168", "c.twitch_created_ts DESC,c.id DESC", rush ? 240 : 120);
        pool("", " AND c.discovery_bucket=MOD(FLOOR(UNIX_TIMESTAMP()/1800),32) AND " + age + "<=720",
             "c.twitch_created_ts DESC,c.id DESC", rush ? 160 : 80);
        if (rush) {
            pool("", " AND " + age + "<=720 AND c.view_count<5000",
                 "c.view_count ASC,c.twitch_created_ts DESC,c.id DESC", 160);
        }
        pool("JOIN recommendation_clip_features f ON f.clip_id=c.id", "", "f.audience_score DESC,c.id DESC",
             160);
        pool("JOIN " + promo_boost_label_subquery("'all','recommended','feed'") + " ON promo.clip_id=c.id",
             "", "promo.promo_boost DESC,c.id DESC", 24);
        if (user_id) {
            pool("JOIN recommendation_predictions p ON p.clip_id=c.id AND p.user_id=" + uid,
                 " AND p.computed_at>UTC_TIMESTAMP()-INTERVAL 6 HOUR", "p.score DESC,c.id DESC", 120);
            pool("",
                 " AND c.game_id IN (SELECT game_id FROM (SELECT c2.game_id FROM "
                 "likes l "
                 "JOIN clips c2 ON c2.id=l.clip_id WHERE l.user_id=" +
                     uid +
                     " AND l.is_like=1 AND c2.game_id IS NOT NULL GROUP BY c2.game_id "
                     "ORDER BY COUNT(*) DESC LIMIT 8) favorites)",
                 "c.twitch_created_ts DESC,c.id DESC", 160);
            pool("",
                 " AND c.game_id IN (SELECT game_id FROM (SELECT c2.game_id FROM "
                 "(SELECT clip_id FROM recommendation_activity WHERE user_id=" +
                     uid +
                     " ORDER BY last_opened_at DESC LIMIT 150) a JOIN clips c2 ON "
                     "c2.id=a.clip_id "
                     "WHERE c2.game_id IS NOT NULL GROUP BY c2.game_id ORDER BY "
                     "COUNT(*) DESC LIMIT 8) recent_games)",
                 "c.twitch_created_ts DESC,c.id DESC", 160);
            pool("",
                 " AND c.broadcaster_id IN (SELECT pe.twitch_id FROM profile_follows "
                 "pf JOIN profile_entities pe "
                 "ON pe.profile_key=pf.profile_key WHERE pf.follower_user_id=" +
                     uid + ")",
                 "c.twitch_created_ts DESC,c.id DESC", 120);
            pool("",
                 " AND c.broadcaster_id IN (SELECT broadcaster_id FROM "
                 "recommendation_twitch_follows WHERE user_id=" +
                     uid + " AND refreshed_at>UTC_TIMESTAMP()-INTERVAL 7 DAY)",
                 "c.twitch_created_ts DESC,c.id DESC", 80);
        }
    }
    const auto q = "WITH pool AS (" + pools.str() + ") SELECT c.*,g.name game_name,promo.promo_lbl," + age +
                   " age_h,COALESCE(f.likes,0),COALESCE(f.dislikes,0),COALESCE(f.unique_"
                   "opens,0),"
                   "COALESCE(f.recent_opens,0),COALESCE(f.commenters,0),COALESCE(f."
                   "dismissals,0),"
                   "COALESCE(f.twitch_velocity,0),IF(p.computed_at>UTC_TIMESTAMP()-INTERVAL "
                   "6 HOUR,p.score,0),"
                   "IF(a.last_opened_at>UTC_TIMESTAMP()-INTERVAL 7 DAY,1,0),IF(a.user_id IS "
                   "NULL,0,1) "
                   "FROM pool JOIN clips c ON c.id=pool.id LEFT JOIN games g ON "
                   "g.id=c.game_id "
                   "LEFT JOIN " +
                   promo_boost_label_subquery("'all','recommended','feed'") +
                   " ON promo.clip_id=c.id "
                   "LEFT JOIN recommendation_clip_features f ON f.clip_id=c.id "
                   "LEFT JOIN recommendation_predictions p ON p.clip_id=c.id AND "
                   "p.user_id=" +
                   uid + " LEFT JOIN recommendation_activity a ON a.clip_id=c.id AND a.user_id=" + uid;
    database::query(conn.raw(), q.c_str());
    database::Result res(mysql_store_result(conn.raw()));
    std::vector<Candidate> result;
    MYSQL_ROW row;
    const unsigned start = mysql_num_fields(res) - 11;
    while ((row = mysql_fetch_row(res))) {
        Candidate c;
        c.clip = row_to_clip(row, res);
        unsigned i = start;
        auto value = [&] {
            const char *s = row[i++];
            return s ? std::stod(s) : 0.0;
        };
        c.age_hours = value();
        c.likes = value();
        c.dislikes = value();
        c.opens = value();
        c.recent_opens = value();
        c.commenters = value();
        c.dismissals = value();
        c.twitch_velocity = value();
        c.collaborative = value();
        c.recently_opened = value();
        c.seen_before = value() > 0;
        c.evergreen = c.age_hours >= 720 &&
                      services::recommendations::approval(c.likes, c.dislikes) >= 0.65 &&
                      (c.clip.view_count >= 50 || c.likes >= 5 || c.opens >= 20);
        c.discovery = c.age_hours <= 168 && c.opens < 30 && c.clip.view_count < 5000 && c.dislikes < 5;
        result.push_back(std::move(c));
    }
    return result;
}

uint64_t ClipRepository::recommendation_catalog_max_id() {
    database::ScopedConnection conn(pool_);
    database::query(conn.raw(), "SELECT COALESCE(MAX(id),0) FROM clips");
    database::Result res(mysql_store_result(conn.raw()));
    auto row = mysql_fetch_row(res);
    return row && row[0] ? std::stoull(row[0]) : 0;
}

uint64_t ClipRepository::refresh_recommendation_features(uint64_t after_id, uint32_t batch_size,
                                                         const std::vector<uint64_t> &only_ids) {
    using services::recommendations::Candidate;
    database::ScopedConnection conn(pool_);
    std::ostringstream selection;
    if (only_ids.empty())
        selection << "c.id>" << after_id;
    else {
        selection << "c.id IN (";
        append_in_numeric(selection, only_ids);
        selection << ')';
    }
    const auto q = "SELECT c.id,"
                   "(SELECT COUNT(*) FROM likes l WHERE l.clip_id=c.id AND l.is_like=1 AND "
                   "l.user_id<>COALESCE(c.author_id,0)),"
                   "(SELECT COUNT(*) FROM likes l WHERE l.clip_id=c.id AND l.is_like=0 AND "
                   "l.user_id<>COALESCE(c.author_id,0)),"
                   "(SELECT COUNT(*) FROM recommendation_activity a WHERE a.clip_id=c.id),"
                   "(SELECT COUNT(*) FROM recommendation_activity a WHERE a.clip_id=c.id "
                   "AND a.last_opened_at>UTC_TIMESTAMP()-INTERVAL 7 DAY),"
                   "(SELECT COUNT(DISTINCT m.user_id) FROM comments m WHERE m.clip_id=c.id "
                   "AND m.user_id<>COALESCE(c.author_id,0)),"
                   "(SELECT COUNT(*) FROM user_feed_blocks b WHERE "
                   "b.target_key=CONCAT('clip:',c.id)),"
                   "(SELECT COUNT(*) FROM recommendation_deliveries d WHERE d.clip_id=c.id "
                   "AND d.impressed_at IS NOT NULL "
                   "AND d.served_at>UTC_TIMESTAMP()-INTERVAL 7 DAY),"
                   "(SELECT IF(MAX(s.view_count)>=MIN(s.view_count) AND "
                   "TIMESTAMPDIFF(SECOND,MIN(s.observed_at),MAX(s.observed_at))>=1800,"
                   "GREATEST(0,CAST(SUBSTRING_INDEX(GROUP_CONCAT(s.view_count ORDER BY "
                   "s.observed_at DESC),',',1) AS SIGNED)-"
                   "CAST(SUBSTRING_INDEX(GROUP_CONCAT(s.view_count ORDER BY "
                   "s.observed_at),',',1) AS SIGNED)) / "
                   "GREATEST(0.5,TIMESTAMPDIFF(SECOND,MIN(s.observed_at),MAX(s.observed_at))"
                   "/3600.0),0) "
                   "FROM clip_twitch_snapshots s WHERE s.clip_id=c.id AND "
                   "s.observed_at>UTC_TIMESTAMP()-INTERVAL 24 HOUR) "
                   "FROM clips c WHERE " +
                   selection.str() + " ORDER BY c.id LIMIT " +
                   std::to_string(std::clamp(batch_size, 1u, 2000u));
    database::query(conn.raw(), q.c_str());
    database::Result res(mysql_store_result(conn.raw()));
    std::ostringstream insert;
    insert << "INSERT INTO "
              "recommendation_clip_features(clip_id,likes,dislikes,unique_opens,"
              "recent_opens,commenters,"
              "dismissals,impressions,twitch_velocity,audience_score) VALUES ";
    MYSQL_ROW row;
    bool first = true;
    uint64_t last_id = 0;
    while ((row = mysql_fetch_row(res))) {
        last_id = std::stoull(row[0]);
        Candidate c;
        c.likes = std::stod(row[1]);
        c.dislikes = std::stod(row[2]);
        c.opens = std::stod(row[3]);
        c.recent_opens = std::stod(row[4]);
        c.commenters = std::stod(row[5]);
        c.dismissals = std::stod(row[6]);
        if (!first)
            insert << ',';
        first = false;
        insert << '(';
        for (unsigned i = 0; i < 9; ++i) {
            if (i)
                insert << ',';
            insert << (row[i] ? row[i] : "0");
        }
        insert << ',' << services::recommendations::audience_quality(c) << ')';
    }
    res.reset();
    if (!first) {
        insert << " ON DUPLICATE KEY UPDATE "
                  "likes=VALUES(likes),dislikes=VALUES(dislikes),unique_opens="
                  "VALUES(unique_opens),"
                  "recent_opens=VALUES(recent_opens),commenters=VALUES(commenters),"
                  "dismissals=VALUES(dismissals),"
                  "impressions=VALUES(impressions),twitch_velocity=VALUES(twitch_"
                  "velocity),audience_score=VALUES(audience_score),"
                  "computed_at=UTC_TIMESTAMP()";
        database::query(conn.raw(), insert.str().c_str());
    }
    return last_id;
}

void ClipRepository::refresh_dirty_recommendation_features() {
    std::vector<uint64_t> ids;
    std::vector<std::pair<uint64_t, uint64_t>> versions;
    {
        database::ScopedConnection conn(pool_);
        database::query(conn.raw(), "SELECT clip_id,generation FROM recommendation_dirty_clips "
                                    "ORDER BY queued_at,clip_id LIMIT 200");
        database::Result res(mysql_store_result(conn.raw()));
        while (auto row = mysql_fetch_row(res)) {
            ids.push_back(std::stoull(row[0]));
            versions.emplace_back(ids.back(), std::stoull(row[1]));
        }
    }
    if (ids.empty())
        return;
    refresh_recommendation_features(0, 200, ids);
    database::ScopedConnection conn(pool_);
    std::ostringstream sql;
    sql << "DELETE FROM recommendation_dirty_clips WHERE ";
    for (size_t i = 0; i < versions.size(); ++i) {
        if (i)
            sql << " OR ";
        sql << "(clip_id=" << versions[i].first << " AND generation=" << versions[i].second << ')';
    }
    // An event committed during feature computation increments generation and
    // remains queued.
    database::query(conn.raw(), sql.str().c_str());
}

void ClipRepository::save_recommendation_twitch_follows(uint64_t user_id,
                                                        const std::vector<std::string> &ids) {
    if (!user_id || ids.size() > 1000)
        return;
    database::ScopedConnection conn(pool_);
    database::Transaction tx(conn.raw());
    // Lock the owner to serialize simultaneous refreshes of this snapshot.
    database::query(conn.raw(),
                    ("SELECT id FROM users WHERE id=" + std::to_string(user_id) + " FOR UPDATE").c_str());
    database::Result owner(mysql_store_result(conn.raw()));
    owner.reset();
    database::query(
        conn.raw(),
        ("DELETE FROM recommendation_twitch_follows WHERE user_id=" + std::to_string(user_id)).c_str());
    if (!ids.empty()) {
        std::ostringstream sql;
        sql << "INSERT IGNORE INTO "
               "recommendation_twitch_follows(user_id,broadcaster_id) VALUES ";
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i)
                sql << ',';
            sql << '(' << user_id << ",'" << database::escape(conn.raw(), ids[i]) << "')";
        }
        database::query(conn.raw(), sql.str().c_str());
    }
    tx.commit();
}

void ClipRepository::refresh_collaborative_predictions(uint64_t user_id) {
    if (!user_id)
        return;
    database::ScopedConnection conn(pool_);
    const auto uid = std::to_string(user_id);
    // At most 100 positive anchors and 50 neighbors. Shared dislikes never create
    // neighbors. Popular anchor clips contribute less. Current negative votes of
    // neighbors DO affect predictions.
    const auto query = "WITH anchors AS (SELECT l.clip_id FROM likes l JOIN clips c ON "
                       "c.id=l.clip_id WHERE l.user_id=" +
                       uid +
                       " AND l.is_like=1 AND c.like_count<=10000 ORDER BY l.created_at DESC "
                       "LIMIT 100), shared_neighbors AS ("
                       "SELECT l.user_id,COUNT(*) shared,SUM(1.0/LOG(2+COALESCE(f.likes,0))) "
                       "strength "
                       "FROM anchors a JOIN likes l ON l.clip_id=a.clip_id AND l.is_like=1 "
                       "LEFT JOIN recommendation_clip_features f ON f.clip_id=a.clip_id "
                       "JOIN users u ON u.id=l.user_id WHERE l.user_id<>" +
                       uid +
                       " AND u.account_blocked=0 AND (u.account_suspended_until IS NULL OR "
                       "u.account_suspended_until<UTC_TIMESTAMP()) "
                       "GROUP BY l.user_id HAVING COUNT(*)>=3 ORDER BY strength DESC LIMIT 50), "
                       "neighbors AS ("
                       "SELECT n.user_id,n.shared,n.strength/SQRT(GREATEST(1,(SELECT COUNT(*) "
                       "FROM likes v "
                       "WHERE v.user_id=n.user_id AND v.is_like=1))) strength FROM "
                       "shared_neighbors n) "
                       "SELECT "
                       "l.clip_id,SUM((n.strength/(n.shared+10.0))*IF(l.is_like,1.0,-1.0))/"
                       "(SUM(n.strength/(n.shared+10.0))+2.0) prediction,COUNT(*) support "
                       "FROM neighbors n JOIN likes l ON l.user_id=n.user_id WHERE "
                       "l.created_at>UTC_TIMESTAMP()-INTERVAL 90 DAY "
                       "AND NOT EXISTS(SELECT 1 FROM likes own WHERE own.user_id=" +
                       uid +
                       " AND own.clip_id=l.clip_id) GROUP BY l.clip_id ORDER BY ABS(prediction) "
                       "DESC LIMIT 300";
    database::query(conn.raw(), query.c_str());
    database::Result res(mysql_store_result(conn.raw()));
    std::vector<std::array<std::string, 3>> predictions;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)))
        predictions.push_back({row[0], row[1], row[2]});
    res.reset();
    database::Transaction tx(conn.raw());
    database::query(conn.raw(), ("DELETE FROM recommendation_predictions WHERE user_id=" + uid).c_str());
    if (!predictions.empty()) {
        std::ostringstream sql;
        sql << "INSERT INTO "
               "recommendation_predictions(user_id,clip_id,score,support) VALUES ";
        for (size_t i = 0; i < predictions.size(); ++i) {
            if (i)
                sql << ',';
            sql << '(' << uid << ',' << predictions[i][0] << ',' << predictions[i][1] << ','
                << predictions[i][2] << ')';
        }
        database::query(conn.raw(), sql.str().c_str());
    }
    tx.commit();
}

void ClipRepository::record_twitch_snapshot(uint64_t clip_id, uint32_t views) {
    database::ScopedConnection conn(pool_);
    database::Transaction tx(conn.raw());
    // One observation per half hour; no synthetic backfill from old view counts.
    const auto sql = "INSERT IGNORE INTO "
                     "clip_twitch_snapshots(clip_id,observed_at,view_count) VALUES(" +
                     std::to_string(clip_id) + ",FROM_UNIXTIME(FLOOR(UNIX_TIMESTAMP()/1800)*1800)," +
                     std::to_string(views) + ")";
    database::query(conn.raw(), sql.c_str());
    if (mysql_affected_rows(conn.raw()) > 0)
        mark_recommendation_dirty(conn.raw(), 0, clip_id);
    tx.commit();
}

void ClipRepository::record_recommendation_delivery(const nlohmann::json &rows, uint64_t user_id,
                                                    const std::string &surface, uint32_t offset) {
    if (rows.empty())
        return;
    database::ScopedConnection conn(pool_);
    std::ostringstream sql;
    sql << "INSERT IGNORE INTO "
           "recommendation_deliveries(delivery_id,user_id,clip_id,surface,source,"
           "position,algorithm_version) VALUES ";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i)
            sql << ',';
        const auto &clip = rows[i]["clip"];
        sql << "('" << database::escape(conn.raw(), clip["delivery_id"].get<std::string>()) << "',"
            << (user_id ? std::to_string(user_id) : "NULL") << ',' << clip["id"].get<uint64_t>() << ",'"
            << database::escape(conn.raw(), surface) << "','"
            << database::escape(conn.raw(), rows[i]["shelf"].get<std::string>()) << "'," << offset + i << ','
            << services::recommendations::version << ')';
    }
    database::query(conn.raw(), sql.str().c_str());
}
void ClipRepository::record_recommendation_impressions(uint64_t user_id,
                                                       const std::vector<std::string> &tokens) {
    if (tokens.empty())
        return;
    database::ScopedConnection conn(pool_);
    std::ostringstream sql;
    sql << "UPDATE recommendation_deliveries SET impressed_at=UTC_TIMESTAMP() "
           "WHERE impressed_at IS NULL "
        << "AND served_at>UTC_TIMESTAMP()-INTERVAL 1 DAY AND "
        << (user_id ? "user_id=" + std::to_string(user_id) : "user_id IS NULL") << " AND delivery_id IN (";
    append_in_quoted(sql, tokens, [&](const std::string &s) { return database::escape(conn.raw(), s); });
    sql << ')';
    database::query(conn.raw(), sql.str().c_str());
}

} // namespace clipstream::repositories
