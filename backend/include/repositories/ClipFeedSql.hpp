#pragma once
#include "repositories/ClipRepository.hpp"
#include <functional>
#include <sstream>

namespace clipstream::repositories::feed_sql {
template <typename T> void append_in_numeric(std::ostringstream &sql, const std::vector<T> &values) {
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            sql << ",";
        sql << values[i];
    }
}

inline void append_in_quoted(std::ostringstream &sql, const std::vector<std::string> &values,
                             const std::function<std::string(const std::string &)> &esc) {
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            sql << ",";
        sql << "'" << esc(values[i]) << "'";
    }
}

inline void append_public_feed_filters(std::ostringstream &sql, const FeedMuteSet *mutes,
                                       const std::function<std::string(const std::string &)> &esc) {
    sql << " AND c.unavailable_at IS NULL AND c.moderation_hidden=0 ";
    if (!mutes)
        return;
    if (!mutes->exclude_clip_ids.empty()) {
        sql << " AND c.id NOT IN (";
        append_in_numeric(sql, mutes->exclude_clip_ids);
        sql << ")";
    }
    if (!mutes->mute_game_ids.empty()) {
        sql << " AND (c.game_id IS NULL OR c.game_id NOT IN (";
        append_in_numeric(sql, mutes->mute_game_ids);
        sql << "))";
    }
    if (!mutes->mute_broadcaster_ids.empty()) {
        sql << " AND (c.broadcaster_id='' OR c.broadcaster_id NOT IN (";
        append_in_quoted(sql, mutes->mute_broadcaster_ids, esc);
        sql << "))";
    }
    if (!mutes->mute_broadcaster_logins.empty()) {
        sql << " AND c.broadcaster_name NOT IN (";
        append_in_quoted(sql, mutes->mute_broadcaster_logins, esc);
        sql << ")";
    }
    for (const auto &kw : mutes->mute_title_keywords) {
        if (kw.empty())
            continue;
        sql << " AND c.title NOT LIKE '%" << esc(kw) << "%'";
    }
    if (!mutes->only_broadcast_langs.empty()) {
        sql << " AND (c.broadcaster_lang='' OR c.broadcaster_lang IN (";
        append_in_quoted(sql, mutes->only_broadcast_langs, esc);
        sql << "))";
    }
    sql << " ";
}

inline std::string promo_boost_subquery(const char *placement_csv) {
    std::ostringstream o;
    o << "(SELECT p.clip_id, MAX(p.boost_score) AS promo_boost FROM "
         "clip_promotions p "
      << "WHERE (p.starts_at IS NULL OR p.starts_at <= UTC_TIMESTAMP()) "
      << "AND (p.ends_at IS NULL OR p.ends_at >= UTC_TIMESTAMP()) "
      << "AND p.placement IN (" << placement_csv << ") GROUP BY p.clip_id) promo";
    return o.str();
}

inline std::string promo_boost_label_subquery(const char *placement_csv) {
    std::ostringstream o;
    o << "(SELECT p.clip_id, MAX(p.boost_score) AS promo_boost, "
      << "SUBSTRING(MAX(CONCAT(LPAD(p.id,12,'0'), IFNULL(p.label,''))), 13) AS "
         "promo_lbl "
      << "FROM clip_promotions p "
      << "WHERE (p.starts_at IS NULL OR p.starts_at <= UTC_TIMESTAMP()) "
      << "AND (p.ends_at IS NULL OR p.ends_at >= UTC_TIMESTAMP()) "
      << "AND p.placement IN (" << placement_csv << ") GROUP BY p.clip_id) promo";
    return o.str();
}

} // namespace clipstream::repositories::feed_sql
