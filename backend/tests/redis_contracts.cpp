#include "services/CacheService.hpp"
#include "services/TwitchService.hpp"
#include <chrono>
#include <climits>
#include <cstdlib>
#include <ctime>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace clipstream::services;
using json = nlohmann::json;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        const auto host = std::getenv("CLIPSTREAM_TEST_REDIS_HOST");
        if (!host) {
            std::cout << "Redis integration skipped\n";
            return 77;
        }
        const auto port = std::getenv("CLIPSTREAM_TEST_REDIS_PORT");
        CacheService first(host, static_cast<uint16_t>(port ? std::stoi(port) : 6379)),
            second(host, static_cast<uint16_t>(port ? std::stoi(port) : 6379));
        check(first.ping_redis(), "Redis unavailable");
        const auto prefix =
            "test:contract:" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        check(first.try_set_nx(prefix + ":lease", "owner-a", 30), "NX acquire failed");
        check(!second.try_set_nx(prefix + ":lease", "owner-b", 30), "Duplicate lease granted");
        check(!second.compare_delete(prefix + ":lease", "owner-b"), "Foreign lease deleted");
        check(!second.compare_expire(prefix + ":lease", "owner-b", 30), "Foreign lease renewed");
        check(first.compare_expire(prefix + ":lease", "owner-a", 30), "Lease owner cannot renew");
        check(first.compare_delete(prefix + ":lease", "owner-a"), "Lease owner cannot release");
        int accepted = 0;
        std::vector<std::future<bool>> requests;
        for (int i = 0; i < 32; ++i)
            requests.push_back(std::async(std::launch::async, [&, i] {
                return (i % 2 ? first : second).acquire_rate_slot(prefix + ":budget", 10, 60);
            }));
        for (auto &r : requests)
            if (r.get())
                ++accepted;
        check(accepted == 10, "Distributed quota exceeded or lost capacity");
        const auto reset = static_cast<int>(std::time(nullptr) + 60);
        first.update_rate_limit(prefix + ":headers", 21, reset, 800);
        check(second.acquire_rate_slot(prefix + ":headers", 600, 60), "Header budget denied available slot");
        check(!first.acquire_rate_slot(prefix + ":headers", 600, 60), "Safety headroom exceeded");
        second.update_rate_limit(prefix + ":headers", 100, reset, 800);
        check(!first.acquire_rate_slot(prefix + ":headers", 600, 60), "Out-of-order response inflated quota");
        int clip_requests = 0;
        TwitchService twitch(
            prefix, "secret", &first, [&](const auto &h, const auto &path, const auto &, bool, const auto &) {
                TwitchService::HttpResult out;
                out.status = 200;
                if (h == "id.twitch.tv")
                    out.body = R"({"access_token":"app","expires_in":3600})";
                else if (path.starts_with("/helix/clips")) {
                    ++clip_requests;
                    out.body =
                        R"({"data":[{"id":"fixture","broadcaster_id":"1","creator_id":"2","view_count":3}],"pagination":{}})";
                } else if (path.starts_with("/helix/users"))
                    out.body = R"({"data":[{"id":"1","login":"one"},{"id":"2","login":"two"}]})";
                else
                    out.body = R"({"data":[{"broadcaster_id":"1","content_classification_labels":[]}]})";
                return out;
            });
        auto low = twitch.fetch_clips(prefix, 3, "", "", "", 0);
        check(low.clips.size() == 1, "Fixture missing");
        auto high = twitch.fetch_clips(prefix, 3, "", "", "", INT_MAX);
        check(high.clips.empty(), "Raw cache ignored min_views");
        check(clip_requests == 1, "Response cache not reused");
        auto bypass = twitch.fetch_clips(prefix, 3, "", "", "", INT_MAX, true);
        check(bypass.clips.empty() && clip_requests == 2, "Bypass response cache failed");
        first.invalidate_pattern(prefix + "*");
        std::cout << "Redis quota, lease ownership and Twitch cache contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
