#pragma once
#include <algorithm>
#include <chrono>
#include <cctype>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace clipstream::utils {
// Exact host allowlist; no suffix matching, credentials, custom ports or local addresses.
inline std::set<std::string> ad_media_hosts(const std::string &input) {
    std::set<std::string> hosts;
    std::istringstream stream(input);
    std::string host;
    while (std::getline(stream, host, ',')) {
        auto first = host.find_first_not_of(" \t");
        auto last = host.find_last_not_of(" \t");
        if (first == std::string::npos) continue;
        host = host.substr(first, last - first + 1);
        std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (!std::regex_match(host, std::regex(R"([a-z0-9](?:[a-z0-9.-]*[a-z0-9])?)")) || host.find('.') == std::string::npos)
            throw std::invalid_argument("Invalid ADS_MEDIA_HOSTS configuration");
        hosts.insert(host);
    }
    return hosts;
}
inline bool safe_ad_url(const std::string &url, const std::set<std::string> *hosts = nullptr) {
    if (url.empty() || url.size() > 2048) return false;
    if (std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32 || c >= 127 || c == '\\'; })) return false;
    static const std::regex pattern(R"(^https://([A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?)(?::443)?(?:[/?#][^\\]*)?$)");
    std::smatch match;
    if (!std::regex_match(url, match, pattern)) return false;
    auto host = match[1].str();
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (host.find('.') == std::string::npos || host.find("..") != std::string::npos ||
        host.ends_with(".localhost") || host.ends_with(".local") ||
        std::all_of(host.begin(), host.end(), [](unsigned char c) { return std::isdigit(c) || c == '.'; })) return false;
    return !hosts || hosts->contains(host);
}
inline std::string ad_date(const nlohmann::json &value) {
    if (value.is_null()) return {};
    if (!value.is_string()) throw std::invalid_argument("Advertisement dates must be UTC strings or null");
    auto s = value.get<std::string>();
    static const std::regex pattern(R"(^(\d{4})-(\d{2})-(\d{2})[T ](\d{2}):(\d{2}):(\d{2})Z?$)");
    std::smatch m;
    if (!std::regex_match(s, m, pattern)) throw std::invalid_argument("Advertisement dates must use YYYY-MM-DDTHH:MM:SSZ");
    using namespace std::chrono;
    const year_month_day date{year{std::stoi(m[1])}, month{unsigned(std::stoi(m[2]))}, day{unsigned(std::stoi(m[3]))}};
    if (!date.ok() || int(date.year()) < 2020 || int(date.year()) > 2100 || std::stoi(m[4]) > 23 || std::stoi(m[5]) > 59 || std::stoi(m[6]) > 59)
        throw std::invalid_argument("Invalid advertisement date");
    s = s.substr(0, 19); s[10] = ' '; return s;
}
inline nlohmann::json validate_ad(const nlohmann::json &input, const std::set<std::string> &hosts) {
    using nlohmann::json;
    if (!input.is_object()) throw std::invalid_argument("Campaign must be an object");
    const std::set<std::string> allowed{"name","type","title","body","advertiser","disclosure_token","image_url","video_url","click_url","enabled","priority","starts_at","ends_at","daily_cap","cooldown_seconds","skip_after_seconds"};
    for (const auto &[key, value] : input.items()) {
        (void)value;
        if (!allowed.contains(key)) throw std::invalid_argument("Unknown campaign field: " + key);
    }
    json out = json::object();
    auto text = [&](const char *key, size_t maximum, bool required) {
        auto it = input.find(key);
        if (it != input.end() && !it->is_string()) throw std::invalid_argument(std::string("Invalid string field: ") + key);
        std::string value = it == input.end() ? "" : it->get<std::string>();
        if (value.size() > maximum || (required && value.find_first_not_of(" \t\n\r") == std::string::npos) ||
            value.find('\0') != std::string::npos || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 && c != '\n' && c != '\t'; }))
            throw std::invalid_argument(std::string("Invalid value for ") + key);
        out[key] = value;
    };
    text("name", 160, true); text("type", 16, true); text("title", 240, true); text("body", 1500, false);
    text("advertiser", 240, true); text("disclosure_token", 240, false);
    text("image_url", 2048, false); text("video_url", 2048, false); text("click_url", 2048, true);
    auto type = out["type"].get<std::string>();
    if (type != "banner" && type != "feed" && type != "preroll") throw std::invalid_argument("Invalid advertisement type");
    if (!safe_ad_url(out["click_url"])) throw std::invalid_argument("Landing URL must be a public HTTPS URL");
    const auto image = out["image_url"].get<std::string>(), video = out["video_url"].get<std::string>();
    if ((!image.empty() && !safe_ad_url(image, &hosts)) || (!video.empty() && !safe_ad_url(video, &hosts)))
        throw std::invalid_argument("Media URL must use HTTPS and an exact ADS_MEDIA_HOSTS host");
    if ((type == "preroll" && video.empty()) || (type != "preroll" && image.empty()))
        throw std::invalid_argument("Advertisement creative media is required");
    if (input.contains("enabled") && !input["enabled"].is_boolean()) throw std::invalid_argument("enabled must be boolean");
    out["enabled"] = input.value("enabled", false);
    auto integer = [&](const char *key, int fallback, int minimum, int maximum) {
        if (input.contains(key) && !input[key].is_number_integer()) throw std::invalid_argument(std::string("Invalid integer field: ") + key);
        const auto value = input.value(key, int64_t(fallback));
        if (value < minimum || value > maximum) throw std::invalid_argument(std::string("Out of range: ") + key);
        out[key] = value;
    };
    integer("priority", 0, 0, 1000); integer("daily_cap", 3, 1, 100);
    integer("cooldown_seconds", 300, 0, 86400); integer("skip_after_seconds", 0, 0, 5);
    const auto start = ad_date(input.value("starts_at", json(nullptr))), end = ad_date(input.value("ends_at", json(nullptr)));
    if (!start.empty() && !end.empty() && end <= start) throw std::invalid_argument("Campaign end must follow start");
    out["starts_at"] = start.empty() ? json(nullptr) : json(start);
    out["ends_at"] = end.empty() ? json(nullptr) : json(end);
    return out;
}
} // namespace clipstream::utils
