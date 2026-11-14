#include "repositories/AdsRepository.hpp"
#include "database/SqlEscape.hpp"
#include "utils/AdValidation.hpp"
#include <algorithm>
#include <vector>

namespace clipstream::repositories {
namespace {
using nlohmann::json;
using database::query;
using database::Result;
std::string quoted(MYSQL *c, const std::string &s) { return "'" + database::escape(c, s) + "'"; }
constexpr const char *columns = "id,name,type,title,body,advertiser,disclosure_token,image_url,video_url,click_url,enabled,priority,starts_at,ends_at,daily_cap,cooldown_seconds,skip_after_seconds,served,impressions,clicks,created_at,updated_at";
json campaign(MYSQL_ROW r) {
    auto text = [&](unsigned i) { return r[i] ? std::string(r[i]) : ""; };
    auto number = [&](unsigned i) -> uint64_t { return r[i] ? std::stoull(r[i]) : 0; };
    auto date = [&](unsigned i) -> json {
        if (!r[i]) return nullptr;
        auto value = text(i); if (value.size() > 10) value[10] = 'T'; return value + "Z";
    };
    return {{"id",number(0)}, {"name",text(1)}, {"type",text(2)}, {"title",text(3)},
            {"body",text(4)}, {"advertiser",text(5)}, {"disclosure","Реклама"},
            {"disclosure_token",text(6)}, {"image_url",text(7)}, {"video_url",text(8)},
            {"click_url",text(9)}, {"enabled",number(10) != 0}, {"priority",number(11)},
            {"starts_at",date(12)}, {"ends_at",date(13)}, {"daily_cap",number(14)},
            {"cooldown_seconds",number(15)}, {"skip_after_seconds",number(16)},
            {"metrics",{{"served",number(17)}, {"impressions",number(18)}, {"clicks",number(19)}}},
            {"created_at",date(20)}, {"updated_at",date(21)}};
}
json creative(const json &full) {
    json out;
    for (auto key : {"id","type","title","body","advertiser","disclosure","disclosure_token","image_url","video_url","click_url","skip_after_seconds"})
        out[key] = full.at(key);
    return out;
}
std::string assignments(MYSQL *c, const json &value) {
    std::string out;
    for (auto key : {"name","type","title","body","advertiser","disclosure_token","image_url","video_url","click_url"}) {
        if (!out.empty()) out += ",";
        out += std::string(key) + "=" + quoted(c, value.at(key).get<std::string>());
    }
    out += ",enabled=" + std::string(value.at("enabled").get<bool>() ? "1" : "0");
    for (auto key : {"priority","daily_cap","cooldown_seconds","skip_after_seconds"})
        out += "," + std::string(key) + "=" + std::to_string(value.at(key).get<unsigned>());
    for (auto key : {"starts_at","ends_at"})
        out += "," + std::string(key) + "=" + (value.at(key).is_null() ? "NULL" : quoted(c,value.at(key).get<std::string>()));
    return out;
}
} // namespace
nlohmann::json AdsRepository::list(unsigned limit, unsigned offset) {
    database::ScopedConnection conn(pool_); auto c = conn.raw();
    limit = std::clamp(limit, 1u, 100u); offset = std::min(offset, 10000u);
    query(c, "SELECT COUNT(*) FROM ad_campaigns WHERE deleted_at IS NULL");
    Result count(mysql_store_result(c)); auto row = mysql_fetch_row(count);
    const auto total = row && row[0] ? std::stoull(row[0]) : 0; count.reset();
    const auto sql = "SELECT " + std::string(columns) + " FROM ad_campaigns WHERE deleted_at IS NULL ORDER BY created_at DESC,id DESC LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
    query(c, sql.c_str()); Result rows(mysql_store_result(c)); json all = json::array();
    while ((row = mysql_fetch_row(rows))) all.push_back(campaign(row));
    return {{"campaigns",all}, {"total",total}, {"limit",limit}, {"offset",offset}};
}
nlohmann::json AdsRepository::metrics() {
    database::ScopedConnection conn(pool_);
    query(conn.raw(),"SELECT COALESCE(SUM(served),0),COALESCE(SUM(impressions),0),COALESCE(SUM(clicks),0),COUNT(*),COALESCE(SUM(deleted_at IS NULL AND enabled=TRUE AND (starts_at IS NULL OR starts_at<=UTC_TIMESTAMP()) AND (ends_at IS NULL OR ends_at>UTC_TIMESTAMP())),0) FROM ad_campaigns");
    Result result(mysql_store_result(conn.raw()));
    auto row = mysql_fetch_row(result);
    auto value = [&](unsigned i) -> uint64_t { return row && row[i] ? std::stoull(row[i]) : 0; };
    return {{"served",value(0)}, {"impressions",value(1)}, {"clicks",value(2)},
            {"campaigns_total",value(3)}, {"campaigns_active",value(4)}};
}
std::optional<nlohmann::json> AdsRepository::find(uint64_t id) {
    database::ScopedConnection conn(pool_); auto c = conn.raw();
    const auto sql = "SELECT " + std::string(columns) + " FROM ad_campaigns WHERE id=" + std::to_string(id) + " AND deleted_at IS NULL";
    query(c, sql.c_str()); Result rows(mysql_store_result(c)); auto row = mysql_fetch_row(rows);
    if (!row) return std::nullopt;
    return campaign(row);
}
uint64_t AdsRepository::create(const nlohmann::json &value) {
    database::ScopedConnection conn(pool_); auto c = conn.raw();
    const auto sql = "INSERT INTO ad_campaigns SET " + assignments(c,value);
    query(c,sql.c_str()); return mysql_insert_id(c);
}
bool AdsRepository::update(uint64_t id, const nlohmann::json &value) {
    database::ScopedConnection conn(pool_); auto c = conn.raw(); database::Transaction tx(c);
    const auto select = "SELECT id FROM ad_campaigns WHERE id=" + std::to_string(id) + " AND deleted_at IS NULL FOR UPDATE";
    query(c,select.c_str()); Result found(mysql_store_result(c)); if (!mysql_fetch_row(found)) return false; found.reset();
    const auto sql = "UPDATE ad_campaigns SET " + assignments(c,value) + " WHERE id=" + std::to_string(id);
    query(c,sql.c_str()); tx.commit(); return true;
}
bool AdsRepository::remove(uint64_t id) {
    database::ScopedConnection conn(pool_); auto c = conn.raw();
    const auto sql = "UPDATE ad_campaigns SET enabled=FALSE,deleted_at=UTC_TIMESTAMP() WHERE id=" + std::to_string(id) + " AND deleted_at IS NULL";
    query(c,sql.c_str()); return mysql_affected_rows(c) > 0;
}
nlohmann::json AdsRepository::serve(const std::string &placement, const std::string &audience, const std::string &token, const std::set<std::string> &media_hosts) {
    database::ScopedConnection conn(pool_); auto c = conn.raw();
    // Bounded opportunistic retention. Aggregate metrics survive delivery proof deletion.
    query(c,"DELETE FROM ad_deliveries WHERE expires_at < UTC_TIMESTAMP() - INTERVAL 7 DAY LIMIT 100");
    query(c,"DELETE FROM ad_daily_caps WHERE day < UTC_DATE() - INTERVAL 7 DAY LIMIT 100");
    const auto candidate_sql = "SELECT id FROM ad_campaigns WHERE type=" + quoted(c,placement) +
        " AND enabled=TRUE AND deleted_at IS NULL AND (starts_at IS NULL OR starts_at<=UTC_TIMESTAMP()) AND (ends_at IS NULL OR ends_at>UTC_TIMESTAMP()) ORDER BY priority DESC,id ASC LIMIT 32";
    query(c,candidate_sql.c_str()); Result candidates(mysql_store_result(c)); std::vector<uint64_t> ids; MYSQL_ROW row;
    while ((row = mysql_fetch_row(candidates))) ids.push_back(std::stoull(row[0]));
    candidates.reset();
    for (auto id : ids) {
        database::Transaction tx(c);
        // Same lock order for serve/event/update: campaign then delivery/cap.
        const auto sql = "SELECT " + std::string(columns) + " FROM ad_campaigns WHERE id=" + std::to_string(id) +
            " AND type=" + quoted(c,placement) + " AND enabled=TRUE AND deleted_at IS NULL AND (starts_at IS NULL OR starts_at<=UTC_TIMESTAMP()) AND (ends_at IS NULL OR ends_at>UTC_TIMESTAMP()) FOR UPDATE";
        query(c,sql.c_str()); Result selected(mysql_store_result(c)); row = mysql_fetch_row(selected);
        if (!row) continue;
        const auto full = campaign(row), ad = creative(full); selected.reset();
        const auto image = ad["image_url"].get<std::string>(), video = ad["video_url"].get<std::string>();
        if ((!image.empty() && !utils::safe_ad_url(image,&media_hosts)) ||
            (!video.empty() && !utils::safe_ad_url(video,&media_hosts)) || !utils::safe_ad_url(ad["click_url"])) continue;
        const auto counter_where = "campaign_id=" + std::to_string(id) + " AND audience_hash=" + quoted(c,audience) + " AND day=UTC_DATE()";
        const auto insert = "INSERT INTO ad_daily_caps(campaign_id,audience_hash,day,served,last_served_at) VALUES(" +
            std::to_string(id) + "," + quoted(c,audience) + ",UTC_DATE(),0,'2000-01-01 00:00:00') ON DUPLICATE KEY UPDATE campaign_id=campaign_id";
        query(c,insert.c_str());
        const auto check = "SELECT served,TIMESTAMPDIFF(SECOND,last_served_at,UTC_TIMESTAMP()) FROM ad_daily_caps WHERE " + counter_where + " FOR UPDATE";
        query(c,check.c_str()); Result counter(mysql_store_result(c)); row = mysql_fetch_row(counter);
        if (!row) throw std::runtime_error("Advertisement cap row unavailable");
        const auto served = std::stoull(row[0]); const auto elapsed = std::stoll(row[1]); counter.reset();
        if (served >= full["daily_cap"].get<uint64_t>() || (served && elapsed < full["cooldown_seconds"].get<int64_t>())) continue;
        const auto increment = "UPDATE ad_daily_caps SET served=served+1,last_served_at=UTC_TIMESTAMP() WHERE " + counter_where;
        query(c,increment.c_str());
        const auto delivery = "INSERT INTO ad_deliveries(token_hash,campaign_id,creative,expires_at) VALUES(" + quoted(c,token) + "," +
            std::to_string(id) + "," + quoted(c,ad.dump()) + ",UTC_TIMESTAMP()+INTERVAL 1 HOUR)";
        query(c,delivery.c_str());
        const auto metric = "UPDATE ad_campaigns SET served=served+1,updated_at=updated_at WHERE id=" + std::to_string(id); query(c,metric.c_str());
        query(c,("SELECT DATE_FORMAT(expires_at,'%Y-%m-%dT%H:%i:%sZ') FROM ad_deliveries WHERE token_hash=" + quoted(c,token)).c_str());
        Result expires(mysql_store_result(c)); row = mysql_fetch_row(expires); const auto expiry = std::string(row[0]); expires.reset();
        tx.commit(); return {{"ad",ad}, {"expires_at",expiry}};
    }
    return {{"ad",nullptr}};
}
std::optional<nlohmann::json> AdsRepository::event(const std::string &token, const std::string &kind) {
    if (kind != "impression" && kind != "click") throw std::invalid_argument("Invalid advertisement event");
    database::ScopedConnection conn(pool_); auto c = conn.raw();
    const auto lookup = "SELECT campaign_id FROM ad_deliveries WHERE token_hash=" + quoted(c,token) + " AND expires_at>UTC_TIMESTAMP()";
    query(c,lookup.c_str()); Result lookup_result(mysql_store_result(c)); auto row = mysql_fetch_row(lookup_result);
    if (!row) return std::nullopt;
    const auto id = std::stoull(row[0]); lookup_result.reset(); database::Transaction tx(c);
    query(c,("SELECT id FROM ad_campaigns WHERE id=" + std::to_string(id) + " FOR UPDATE").c_str());
    Result campaign_row(mysql_store_result(c)); if (!mysql_fetch_row(campaign_row)) return std::nullopt; campaign_row.reset();
    query(c,("SELECT creative,impression_at,click_at FROM ad_deliveries WHERE token_hash=" + quoted(c,token) + " AND expires_at>UTC_TIMESTAMP() FOR UPDATE").c_str());
    Result delivery(mysql_store_result(c)); row = mysql_fetch_row(delivery); if (!row) return std::nullopt;
    const auto ad = json::parse(row[0]); const bool impressed = row[1] != nullptr, clicked = row[2] != nullptr; delivery.reset();
    if (kind == "click" && !impressed) return json{{"recorded",false}, {"click_url",ad["click_url"]}};
    const bool duplicate = kind == "impression" ? impressed : clicked;
    if (!duplicate) {
        const auto field = kind == "impression" ? "impression_at" : "click_at", metric = kind == "impression" ? "impressions" : "clicks";
        query(c,("UPDATE ad_deliveries SET " + std::string(field) + "=UTC_TIMESTAMP() WHERE token_hash=" + quoted(c,token)).c_str());
        query(c,("UPDATE ad_campaigns SET " + std::string(metric) + "=" + metric + "+1,updated_at=updated_at WHERE id=" + std::to_string(id)).c_str());
    }
    tx.commit(); json out = {{"recorded",!duplicate}}; if (kind == "click") out["click_url"] = ad["click_url"]; return out;
}
} // namespace clipstream::repositories
