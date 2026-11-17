#include "database/Migrations.hpp"
#include "repositories/ClipRepository.hpp"
#include "repositories/SyncRepository.hpp"
#include "utils/Environment.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace clipstream;

static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

static void exec(database::ConnectionPool& pool, const std::string& sql) {
    database::ScopedConnection connection(pool);
    database::query(connection.raw(), sql.c_str());
}

static std::vector<uint64_t> numbers(database::ConnectionPool& pool, const std::string& sql) {
    database::ScopedConnection connection(pool);
    database::query(connection.raw(), sql.c_str());
    database::Result result(mysql_store_result(connection.raw()));
    std::vector<uint64_t> values;
    while (auto row = mysql_fetch_row(result))
        values.push_back(std::stoull(row[0]));
    return values;
}

struct Fixtures {
    database::ConnectionPool& pool;
    uint64_t game{};
    unsigned sequence{};

    explicit Fixtures(database::ConnectionPool& value) : pool(value) {
        exec(pool, "INSERT INTO games(twitch_id,name) VALUES('query-contracts','Query contracts')");
        game = numbers(pool, "SELECT LAST_INSERT_ID()").at(0);
    }
    ~Fixtures() {
        try {
            exec(pool, "DROP TRIGGER IF EXISTS query_contract_generation");
            clear();
            exec(pool, "DELETE FROM games WHERE id=" + std::to_string(game));
        } catch (const std::exception& e) {
            std::cerr << "Fixture cleanup failed: " << e.what() << '\n';
        }
    }
    void clear() {
        // Promotions predate the FK-backed recommendation tables.
        exec(pool, "DELETE FROM clip_promotions WHERE clip_id IN "
                   "(SELECT id FROM clips WHERE game_id=" +
                       std::to_string(game) + ")");
        exec(pool, "DELETE FROM clips WHERE game_id=" + std::to_string(game));
    }
    uint64_t clip(const std::string& twitch_date = "NULL", unsigned views = 100,
                  const std::string& imported = "UTC_TIMESTAMP()") {
        exec(pool, "INSERT INTO clips(twitch_clip_id,title,thumbnail_url,embed_url,broadcaster_name,"
                   "game_id,view_count,twitch_created_at,created_at) VALUES('query-" +
                       std::to_string(++sequence) + "','Query fixture','','','query-channel'," +
                       std::to_string(game) + ',' + std::to_string(views) + ',' + twitch_date + ',' +
                       imported + ')');
        return numbers(pool, "SELECT LAST_INSERT_ID()").at(0);
    }
};

static void hourly_contracts(database::ConnectionPool& pool, Fixtures& fixtures) {
    repositories::SyncRepository sync(pool);
    auto hours = sync.dashboard().at("hourly");
    check(hours.size() == 24, "Empty dashboard must retain all 24 hours");
    check(hours.front().at("hour") == "2026-11-16T13:00:00Z" &&
              hours.back().at("hour") == "2026-11-17T12:00:00Z",
          "Dashboard interval must be aligned to UTC hours");
    for (const auto& hour : hours)
        check(hour.at("inserted") == 0, "Empty hour must be zero");

    fixtures.clip("NULL", 100, "'2026-11-16 12:59:59'"); // Before the inclusive lower bound.
    fixtures.clip("NULL", 100, "'2026-11-16 13:00:00'");
    fixtures.clip("NULL", 100, "'2026-11-17 11:59:59'");
    const auto hidden = fixtures.clip("NULL", 100, "'2026-11-17 12:00:00'");
    exec(pool, "UPDATE clips SET moderation_hidden=1,unavailable_at=UTC_TIMESTAMP() WHERE id=" +
                   std::to_string(hidden));
    fixtures.clip("NULL", 100, "'2026-11-17 12:59:59'"); // Entire current hour is included.
    fixtures.clip("NULL", 100, "'2026-11-17 13:00:00'"); // Exclusive upper bound.
    hours = sync.dashboard().at("hourly");
    check(hours.size() == 24, "Sparse dashboard must retain all hours");
    for (size_t i = 0; i < hours.size(); ++i) {
        const unsigned expected = i == 0 || i == 22 ? 1 : i == 23 ? 2 : 0;
        check(hours[i].at("inserted") == expected, "Import counted in the wrong hour");
        if (i)
            check(hours[i - 1].at("hour").get<std::string>() < hours[i].at("hour").get<std::string>(),
                  "Dashboard hours must be ordered");
    }
    fixtures.clear();
}

static void queue_contracts(database::ConnectionPool& pool, Fixtures& fixtures) {
    repositories::ClipRepository clips(pool);
    clips.refresh_dirty_recommendation_features();
    check(numbers(pool, "SELECT COUNT(*) FROM recommendation_clip_features").at(0) == 0,
          "Empty queue must not produce features");
    for (unsigned i = 0; i < 205; ++i) {
        const auto id = fixtures.clip();
        // Reverse insertion order, with ties requiring the clip_id tie-breaker.
        exec(pool, "INSERT INTO recommendation_dirty_clips(clip_id,generation,queued_at) VALUES(" +
                       std::to_string(id) + ",7,FROM_UNIXTIME(" + std::to_string(1000000000 + (204 - i) / 3) +
                       "))");
    }
    const auto ordered = numbers(pool, "SELECT clip_id FROM recommendation_dirty_clips "
                                       "ORDER BY queued_at,clip_id");
    clips.refresh_dirty_recommendation_features();
    auto processed = numbers(pool, "SELECT clip_id FROM recommendation_clip_features ORDER BY clip_id");
    auto expected = std::vector<uint64_t>(ordered.begin(), ordered.begin() + 200);
    std::sort(expected.begin(), expected.end());
    check(processed == expected, "Queue must process precisely the earliest 200 entries");
    check(numbers(pool, "SELECT clip_id FROM recommendation_dirty_clips ORDER BY queued_at,clip_id") ==
              std::vector<uint64_t>(ordered.begin() + 200, ordered.end()),
          "Queue must retain entries outside the batch");
    clips.refresh_dirty_recommendation_features();
    check(numbers(pool, "SELECT COUNT(*) FROM recommendation_dirty_clips").at(0) == 0,
          "Second batch must drain the queue");
    fixtures.clear();

    const auto id = fixtures.clip();
    exec(pool,
         "INSERT INTO recommendation_dirty_clips(clip_id,generation) VALUES(" + std::to_string(id) + ",7)");
    // Deterministically model a new event after the generation snapshot was read.
    exec(pool, "CREATE TRIGGER query_contract_generation BEFORE INSERT ON recommendation_clip_features "
               "FOR EACH ROW UPDATE recommendation_dirty_clips SET generation=generation+1 "
               "WHERE clip_id=NEW.clip_id AND generation=7");
    clips.refresh_dirty_recommendation_features();
    check(numbers(pool, "SELECT generation FROM recommendation_dirty_clips WHERE clip_id=" +
                            std::to_string(id)) == std::vector<uint64_t>{8},
          "An event during feature refresh must survive generation-guarded deletion");
    exec(pool, "DROP TRIGGER query_contract_generation");
    clips.refresh_dirty_recommendation_features();
    check(numbers(pool, "SELECT COUNT(*) FROM recommendation_dirty_clips").at(0) == 0,
          "Preserved generation must be processed on the next pass");
    fixtures.clear();
}

static void feed_contracts(database::ConnectionPool& pool, Fixtures& fixtures) {
    repositories::ClipRepository clips(pool);
    const auto day = fixtures.clip("'2026-11-16T12:34:56Z'", 2);
    const auto before_day = fixtures.clip("'2026-11-16T12:34:55Z'", 10000);
    const auto recent = fixtures.clip("'2026-11-17T11:00:00Z'", 100);
    fixtures.clip("'2026-11-17T12:00:00Z'", 1);
    const auto week = fixtures.clip("'2026-11-10T12:34:56Z'", 10);
    fixtures.clip("'2026-11-10T12:34:55Z'", 10);
    const auto month = fixtures.clip("'2026-10-18T12:34:56Z'", 30);
    fixtures.clip("'2026-10-18T12:34:55Z'", 30);
    const auto missing = fixtures.clip();
    const auto blank = fixtures.clip("''");
    const auto whitespace = fixtures.clip("'   '");
    const auto hidden = fixtures.clip("'2026-11-17T12:00:00Z'", 10000);
    const auto unavailable = fixtures.clip("'2026-11-17T12:00:00Z'", 10000);
    exec(pool, "UPDATE clips SET moderation_hidden=1 WHERE id=" + std::to_string(hidden));
    exec(pool, "UPDATE clips SET unavailable_at=UTC_TIMESTAMP() WHERE id=" + std::to_string(unavailable));

    repositories::ClipFilter filter;
    filter.game_ids = {fixtures.game};
    filter.period = repositories::TimePeriod::Today;
    filter.sort = repositories::ClipSort::Recent;
    auto ids = [&](uint32_t limit = 50, uint32_t offset = 0,
                   const repositories::FeedMuteSet* mutes = nullptr) {
        std::vector<uint64_t> result;
        for (const auto& clip : clips.find_filtered(filter, limit, offset, mutes))
            result.push_back(clip.id);
        return result;
    };
    check(ids() == std::vector<uint64_t>({recent, day}), "Day boundary or recent ordering changed");
    check(ids(1, 1) == std::vector<uint64_t>{day}, "Feed pagination changed");
    filter.period = repositories::TimePeriod::Week;
    check(ids() == std::vector<uint64_t>({recent, before_day, week}),
          "Week boundary or minimum views changed");
    filter.period = repositories::TimePeriod::Month;
    // The high-view clip just outside Today remains eligible for Month.
    const auto monthly = ids();
    check(std::find(monthly.begin(), monthly.end(), month) != monthly.end() && monthly.size() == 3,
          "Month boundary or minimum views changed");
    filter.period = repositories::TimePeriod::All;
    const auto all = ids();
    for (const auto id : {missing, blank, whitespace})
        check(std::find(all.begin(), all.end(), id) != all.end(), "All-time feed lost a missing date");
    for (const auto id : {hidden, unavailable})
        check(std::find(all.begin(), all.end(), id) == all.end(), "Visibility filters were bypassed");

    exec(pool, "INSERT INTO clip_promotions(clip_id,boost_score,placement) VALUES(" + std::to_string(day) +
                   ",20,'feed')");
    filter.period = repositories::TimePeriod::Today;
    filter.sort = repositories::ClipSort::Popular;
    check(ids() == std::vector<uint64_t>({day, recent}), "Popular feed lost promotion ordering");
    repositories::FeedMuteSet mutes;
    mutes.exclude_clip_ids.push_back(day);
    check(ids(50, 0, &mutes) == std::vector<uint64_t>{recent}, "Promotion bypassed feed mutes");
    bool invalid_rejected = false;
    try {
        fixtures.clip("'not-a-date'");
    } catch (const database::Error&) {
        invalid_rejected = true;
    }
    check(invalid_rejected, "Invalid dates must be rejected by the generated timestamp column");
    fixtures.clear();

    // Every sort key ties: pagination must use the same ID tie-break in both stages.
    std::vector<uint64_t> tied;
    for (unsigned i = 0; i < 8; ++i)
        tied.push_back(fixtures.clip("'2026-11-17T11:00:00Z'", 10));
    std::reverse(tied.begin(), tied.end());
    filter.period = repositories::TimePeriod::Week;
    for (const auto sort : {repositories::ClipSort::Recent, repositories::ClipSort::Popular,
                            repositories::ClipSort::Duration}) {
        filter.sort = sort;
        check(ids() == tied, "Equal-key ordering is unstable after hydration");
        check(ids(3, 2) == std::vector<uint64_t>(tied.begin() + 2, tied.begin() + 5),
              "Hydration changed the selected page");
    }
    mutes.exclude_clip_ids = {tied[0], tied[1]};
    check(ids(3, 0, &mutes) == std::vector<uint64_t>(tied.begin() + 2, tied.begin() + 5),
          "Mute filters must apply before LIMIT");
    const auto hydrated = clips.find_filtered(filter, 3, 2);
    check(hydrated.size() == 3 && hydrated[0].game_name == "Query contracts" &&
              hydrated[0].title == "Query fixture" && hydrated[0].view_count == 10,
          "Late hydration lost clip or game fields");
    filter.game_ids.clear();
    filter.sort = repositories::ClipSort::Recent;
    const auto promoted = tied.back();
    exec(pool, "INSERT INTO clip_promotions(clip_id,boost_score,placement) VALUES(" +
                   std::to_string(promoted) + ",20,'feed')");
    check(ids(1) == std::vector<uint64_t>{promoted}, "Index prefix lost a promoted timestamp tie");
    check(ids(2, 1, &mutes) == std::vector<uint64_t>({tied[2], tied[3]}),
          "Indexed pagination lost its ordering or mute filters");
    check(ids(1, 99).empty(), "Indexed pagination must allow offsets beyond the result set");
    fixtures.clear();

    const auto title_match = fixtures.clip("'2026-11-17T11:00:00Z'", 10);
    const auto channel_match = fixtures.clip("'2026-11-17T11:00:00Z'", 1000);
    exec(pool, "UPDATE clips SET title='needle' WHERE id=" + std::to_string(title_match));
    exec(pool, "UPDATE clips SET broadcaster_name='needle' WHERE id=" + std::to_string(channel_match));
    filter.search = "needle";
    filter.sort = repositories::ClipSort::Popular;
    check(ids(1) == std::vector<uint64_t>{title_match}, "Search relevance must precede popularity");
    check(ids(1, 1) == std::vector<uint64_t>{channel_match}, "Search pagination changed");
    fixtures.clear();
}

int main() {
    try {
        // These fixtures need their own empty database, separate from other suites.
        const auto name = utils::env("CLIPSTREAM_QUERY_TEST_DATABASE");
        if (name.empty())
            return 77;
        check(name.ends_with("_test"), "Refusing non-test database");
        database::DbConfig cfg{.host = utils::env("MYSQL_HOST", "127.0.0.1"),
                               .port = static_cast<uint16_t>(std::stoul(utils::env("MYSQL_PORT", "3306"))),
                               .database = name,
                               .user = utils::env("MYSQL_USER"),
                               .password = utils::env("MYSQL_PASSWORD"),
                               .pool_size = 1,
                               .read_timeout_seconds = 60,
                               .write_timeout_seconds = 60};
        database::ConnectionPool pool(cfg);
        database::migrate(pool, utils::env("MIGRATIONS_DIR", "database/migrations"));
        check(numbers(pool, "SELECT COUNT(*) FROM clips").at(0) == 0, "Empty query test database required");
        exec(pool, "SET timestamp=UNIX_TIMESTAMP('2026-11-17 12:34:56')");
        Fixtures fixtures(pool);
        hourly_contracts(pool, fixtures);
        queue_contracts(pool, fixtures);
        feed_contracts(pool, fixtures);
        std::cout << "Query optimization contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
