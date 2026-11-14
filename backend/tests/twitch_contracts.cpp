#include "services/TwitchService.hpp"
#include <climits>
#include <iostream>
#include <stdexcept>

using namespace clipstream::services;
using json = nlohmann::json;
static void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        int pages = 0;
        bool encoded = false;
        bool refreshed = false;
        TwitchService twitch(
            "client", "Secret+/=", nullptr,
            [&](const auto &host, const auto &target, const auto &body, bool, const auto &headers) {
                TwitchService::HttpResult result;
                result.status = 200;
                if (host == "id.twitch.tv" && target == "/oauth2/token") {
                    encoded = body.find("Secret%2B%2F%3D") != std::string::npos;
                    if (body.find("grant_type=refresh_token") != std::string::npos)
                        check(body.find("refresh_token=AbC%2B%2F%3D") != std::string::npos,
                              "Refresh token form encoding changed case/value");
                    result.body = R"({"access_token":"app","refresh_token":"new","expires_in":3600})";
                } else if (target == "/oauth2/validate") {
                    result.status = 401;
                    result.body = R"({"message":"invalid"})";
                } else if (target.starts_with("/helix/channels/followed")) {
                    ++pages;
                    bool fresh = false;
                    for (const auto &[name, value] : headers)
                        if (name == "Authorization" && value == "Bearer fresh")
                            fresh = true;
                    if (!fresh) {
                        result.status = 401;
                        result.body = R"({"error":"Unauthorized"})";
                    } else if (target.find("after=") == std::string::npos)
                        result.body =
                            R"({"data":[{"broadcaster_id":"1"}],"pagination":{"cursor":"NEXT+/="}})";
                    else {
                        check(target.find("after=NEXT%2B%2F%3D") != std::string::npos,
                              "Cursor not percent encoded");
                        result.body = R"({"data":[{"broadcaster_id":"2"}],"pagination":{}})";
                    }
                } else if (target.starts_with("/helix/clips"))
                    result.body =
                        R"({"data":[{"id":"clip","broadcaster_id":"1","creator_id":"2","broadcaster_name":"Display","creator_name":"DisplayCreator","view_count":3}],"pagination":{"cursor":"more"}})";
                else if (target.starts_with("/helix/users"))
                    result.body =
                        R"({"data":[{"id":"1","login":"broadcaster","display_name":"Display"},{"id":"2","login":"creator","display_name":"DisplayCreator"}]})";
                else if (target.starts_with("/helix/channels?"))
                    result.body =
                        R"({"data":[{"broadcaster_id":"1","content_classification_labels":["MatureGame"]}]})";
                else {
                    result.status = 503;
                    result.body = R"({"error":"unavailable"})";
                }
                return result;
            });
        twitch.set_user_token_refresh_handler([&](const std::string &value) {
            check(value == "old", "Unexpected refresh input");
            refreshed = true;
            return "fresh";
        });
        auto token = twitch.refresh_user_token("AbC+/=");
        check(token && encoded, "OAuth encoding failed");
        check(!twitch.validate_user_token("revoked"), "401 validation must return revoked");
        auto follows = twitch.get_followed_channels("old", "user", 100);
        check(follows.size() == 2 && pages == 4 && refreshed, "Followed pagination / refresh failed");
        auto none = twitch.fetch_clips("game", 100, "", "", "", INT_MAX, true);
        check(none.clips.empty() && none.cursor == "more", "Filtered empty page lost cursor");
        auto clips = twitch.fetch_clips("game", 100, "", "", "", 0, true);
        check(clips.clips.size() == 1, "Clip missing");
        check(clips.clips[0].creator_login == "creator" && clips.clips[0].broadcaster_login == "broadcaster",
              "Display name used as login");
        check(clips.clips[0].is_mature, "CCL did not restrict mature clip");
        try {
            twitch.search_channels("test");
            throw std::runtime_error("Upstream failure silently became empty");
        } catch (const TwitchError &e) {
            check(e.status == 502, "Wrong upstream failure status");
        }
        std::cout << "Twitch contract checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
