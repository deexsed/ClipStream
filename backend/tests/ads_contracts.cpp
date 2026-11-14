#include "utils/AdValidation.hpp"
#include "utils/Environment.hpp"
#include "utils/Crypto.hpp"
#include "repositories/AdsRepository.hpp"
#include "database/Migrations.hpp"
#include <future>
#include <iostream>
#include <stdexcept>

using namespace clipstream;
using nlohmann::json;
namespace {
void require(bool okay, const char *message) { if (!okay) throw std::runtime_error(message); }
template<class F> void rejects(F action, const char *message) {
    try { action(); } catch (const std::invalid_argument &) { return; }
    throw std::runtime_error(message);
}
std::string proof() {
    unsigned char bytes[32];
    if (RAND_bytes(bytes,sizeof bytes) != 1) throw std::runtime_error("Random unavailable");
    return utils::hex(bytes,sizeof bytes);
}
json fixture() {
    return {{"name","Contract campaign"}, {"type","banner"}, {"title","A real advertisement"},
        {"advertiser","Contract advertiser"}, {"image_url","https://media.example.test/image.png"},
        {"click_url","https://advertiser.example.test/offer"}, {"enabled",true},
        {"daily_cap",1}, {"cooldown_seconds",0}};
}
} // namespace
int main() {
    try {
        auto hosts = utils::ad_media_hosts(" media.example.test, VIDEO.example.test ");
        require(hosts.size() == 2 && hosts.contains("video.example.test"),"Exact hosts normalization failed");
        require(utils::safe_ad_url("https://media.example.test/x?y=1",&hosts),"Valid asset rejected");
        for (const auto &url : {"javascript:alert(1)","http://media.example.test/file","https://media.example.test.evil.test/file", "https://evil.test/file", "https://media.example.test@evil.test/file", "https://media.example.test:8080/file", "https://media.example.test\\@evil.test/file", "https://127.0.0.1/file", "https://localhost/file"})
            require(!utils::safe_ad_url(url,&hosts),"Unsafe media URL accepted");
        auto ad = utils::validate_ad(fixture(),hosts);
        require(ad["daily_cap"] == 1 && ad["starts_at"].is_null(),"Campaign defaults invalid");
        for (const auto &patch : std::vector<json>{{{"enabled","true"}},{{"daily_cap",0}},{{"daily_cap",1.5}},{{"priority",-1}},{{"skip_after_seconds",6}},{{"unknown",true}},{{"advertiser",""}},{{"click_url","data:text/html,hello"}},{{"starts_at","2026-02-29T00:00:00Z"}},{{"starts_at","2026-12-01T00:00:00Z"},{"ends_at","2026-11-01T00:00:00Z"}}}) {
            auto value = fixture(); value.update(patch);
            rejects([&] { utils::validate_ad(value,hosts); },"Invalid campaign accepted");
        }
        auto video = fixture(); video["type"] = "preroll";
        rejects([&] { utils::validate_ad(video,hosts); },"Preroll without video accepted");
        video["video_url"] = "https://video.example.test/ad.mp4";
        require(utils::validate_ad(video,hosts)["type"] == "preroll","Valid preroll rejected");
        std::cout << "Advertisement input and URL contracts passed\n";

        const auto name = utils::env("CLIPSTREAM_TEST_DATABASE");
        if (name.empty()) { std::cout << "Database advertisement contracts skipped (CLIPSTREAM_TEST_DATABASE unset)\n"; return 0; }
        if (!name.ends_with("_test")) throw std::runtime_error("Test database must end in _test");
        database::DbConfig cfg{.host=utils::env("MYSQL_HOST","127.0.0.1"),
            .port=static_cast<uint16_t>(std::stoul(utils::env("MYSQL_PORT","3306"))), .database=name,
            .user=utils::env("MYSQL_USER","clipstream"), .password=utils::env("MYSQL_PASSWORD"), .pool_size=4};
        database::ConnectionPool pool(cfg);
        database::migrate(pool,utils::env("MIGRATIONS_DIR","database/migrations"));
        repositories::AdsRepository ads(pool);
        // Distinct session hash avoids coupling to any previous test invocation.
        const auto audience = proof(), first = proof(), second = proof();
        const auto id = ads.create(ad);
        try {
            auto found = ads.find(id); require(found.has_value() && found->at("metrics")["impressions"] == 0,"Fabricated initial metrics");
            auto serve = [&](const std::string &token) { return ads.serve("banner",audience,token,hosts); };
            auto a = std::async(std::launch::async,[&] { return serve(first); });
            auto b = std::async(std::launch::async,[&] { return serve(second); });
            const auto ar = a.get(), br = b.get();
            // Isolate this test's candidate by checking the fixture ID. Tests require a fresh test DB.
            const bool a_fixture = !ar["ad"].is_null() && ar["ad"]["id"] == id;
            const bool b_fixture = !br["ad"].is_null() && br["ad"]["id"] == id;
            require(a_fixture != b_fixture,"Concurrent daily cap exceeded or fresh test DB required");
            const auto delivered = a_fixture ? first : second;
            require(!ads.event(proof(),"impression"),"Unserved event fabricated a metric");
            auto click_first = ads.event(delivered,"click"); require(click_first && !click_first->at("recorded").get<bool>(),"Click counted before observed impression");
            auto i1 = std::async(std::launch::async,[&] { return ads.event(delivered,"impression"); });
            auto i2 = std::async(std::launch::async,[&] { return ads.event(delivered,"impression"); });
            require(i1.get()->at("recorded").get<bool>() != i2.get()->at("recorded").get<bool>(),"Concurrent impression replay counted twice");
            require(ads.event(delivered,"click")->at("recorded").get<bool>(),"Click not counted");
            require(!ads.event(delivered,"click")->at("recorded").get<bool>(),"Click replay counted twice");
            found = ads.find(id);
            require(found->at("metrics") == json{{"served",1},{"impressions",1},{"clicks",1}},"Metrics did not match acknowledged events");
            {
                database::ScopedConnection conn(pool);
                database::query(conn.raw(),("UPDATE ad_deliveries SET expires_at=UTC_TIMESTAMP()-INTERVAL 1 SECOND WHERE token_hash='" + delivered + "'").c_str());
            }
            require(!ads.event(delivered,"impression"),"Expired advertisement proof accepted");
            require(ads.metrics()["impressions"].get<uint64_t>() >= 1,"Aggregate advertisement metrics missing");
            auto off = ad; off["enabled"] = false; require(ads.update(id,off),"Update failed");
            require(ads.serve("banner",proof(),proof(),hosts)["ad"].is_null(),"Disabled campaign served");
            off["enabled"] = true; off["starts_at"] = "2099-01-01 00:00:00"; ads.update(id,off);
            require(ads.serve("banner",proof(),proof(),hosts)["ad"].is_null(),"Future campaign served");
            off["starts_at"] = nullptr; off["ends_at"] = "2020-01-01 00:00:00"; ads.update(id,off);
            require(ads.serve("banner",proof(),proof(),hosts)["ad"].is_null(),"Expired campaign served");
            off["ends_at"] = nullptr;
            off["starts_at"] = nullptr; off["daily_cap"] = 3; off["cooldown_seconds"] = 3600; ads.update(id,off);
            const auto fresh = proof(); require(!ads.serve("banner",fresh,proof(),hosts)["ad"].is_null(),"Eligible campaign not served");
            require(ads.serve("banner",fresh,proof(),hosts)["ad"].is_null(),"Cooldown did not apply");
            require(ads.serve("banner",proof(),proof(),{})["ad"].is_null(),"Removed media host still served");
            require(ads.remove(id) && !ads.find(id) && !ads.remove(id),"Delete lifecycle failed");
            require(ads.serve("banner",proof(),proof(),hosts)["ad"].is_null(),"Deleted campaign served");
        } catch (...) { ads.remove(id); throw; }
        auto low = ad, high = ad; high["priority"] = 10;
        const auto low_id = ads.create(low), high_id = ads.create(high);
        try {
            const auto audience2 = proof();
            require(ads.serve("banner",audience2,proof(),hosts)["ad"]["id"] == high_id,"Campaign priority ignored");
            require(ads.serve("banner",audience2,proof(),hosts)["ad"]["id"] == low_id,"Capped campaign prevented eligible fallback");
            require(ads.serve("banner",audience2,proof(),hosts)["ad"].is_null(),"All caps exhausted but campaign still served");
            ads.remove(low_id); ads.remove(high_id);
        } catch (...) { ads.remove(low_id); ads.remove(high_id); throw; }
        std::cout << "Advertisement CRUD, schedule, shared caps and event idempotence contracts passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
