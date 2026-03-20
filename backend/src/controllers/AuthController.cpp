#include "controllers/AuthController.hpp"
#include <iostream>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <algorithm>

namespace clipstream::controllers {

namespace {
/// Синхронизировать с frontend: constants/legalConsent.js (LEGAL_CONSENT_VERSION)
constexpr const char* kLegalConsentVersion = "2026-03-20";

std::string now_plus_seconds_iso(int sec) {
    auto tp = std::chrono::system_clock::now() + std::chrono::seconds(sec);
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}
} // namespace

AuthController::AuthController(services::AuthService& auth_service,
                               services::TwitchService& twitch_service,
                               services::CacheService& cache_service,
                               const std::string& redirect_uri)
    : auth_(auth_service), twitch_(twitch_service), cache_(cache_service), redirect_uri_(redirect_uri) {}

void AuthController::register_routes(server::Router& router) {
    router.post("/auth/register", [this](const server::Request& r) { return handle_register(r); });
    router.post("/auth/login",    [this](const server::Request& r) { return handle_login(r); });
    router.get("/auth/me",        [this](const server::Request& r) { return handle_me(r); });
    router.get("/auth/twitch/url",[this](const server::Request& r) { return handle_twitch_url(r); });
    router.post("/auth/twitch",   [this](const server::Request& r) { return handle_twitch_login(r); });
}

server::Response AuthController::handle_register(const server::Request& req) {
    auto& b = req.body;
    if (!b.contains("username") || !b.contains("email") || !b.contains("password"))
        return {400, {{"error", "Missing fields: username, email, password"}}};

    auto result = auth_.register_user(b["username"], b["email"], b["password"]);
    if (!result) return {409, {{"error", "User already exists"}}};

    return {201, {{"token", result->token}, {"user", result->user}}};
}

server::Response AuthController::handle_login(const server::Request& req) {
    auto& b = req.body;
    if (!b.contains("email") || !b.contains("password"))
        return {400, {{"error", "Missing fields: email, password"}}};

    auto result = auth_.login(b["email"], b["password"]);
    if (!result) return {401, {{"error", "Invalid credentials"}}};

    return {200, {{"token", result->token}, {"user", result->user}}};
}

server::Response AuthController::handle_me(const server::Request& req) {
    if (req.user_id == 0) return {401, {{"error", "Unauthorized"}}};
    auto profile = auth_.get_user_profile(req.user_id);
    if (profile.contains("error")) return {404, {{"error", "User not found"}}};
    return {200, profile};
}

std::string AuthController::generate_state() {
    unsigned char buf[16];
    RAND_bytes(buf, sizeof(buf));
    std::ostringstream oss;
    for (auto b : buf) oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return oss.str();
}

server::Response AuthController::handle_twitch_url(const server::Request&) {
    auto state = generate_state();
    cache_.set("oauth_state:" + state, "1", 300);

    auto url = twitch_.get_auth_url(redirect_uri_) + "&state=" + state;
    return {200, {{"url", url}, {"state", state}}};
}

server::Response AuthController::handle_twitch_login(const server::Request& req) {
    auto& b = req.body;
    if (!b.contains("code") || !b.contains("state"))
        return {400, {{"error", "Missing fields: code, state"}}};

    bool has_consent = b.contains("consent") && b["consent"].is_object();
    std::string cver;
    std::string accepted_at;
    if (has_consent) {
        const auto& consent = b["consent"];
        cver = consent.value("version", std::string{});
        accepted_at = consent.value("accepted_at", std::string{});
        if (cver != kLegalConsentVersion || accepted_at.empty())
            return {400, {{"error", "Неверная или устаревшая версия согласия с документами"}}};
    }

    std::string code = b["code"];
    std::string state = b["state"];

    auto ok = cache_.get("oauth_state:" + state);
    if (!ok) return {401, {{"error", "Invalid or expired state"}}};
    cache_.del("oauth_state:" + state);

    std::cout << "[Auth] Twitch OAuth exchange for code...\n";

    auto token_set = twitch_.exchange_code_tokens(code, redirect_uri_);
    if (!token_set || token_set->access_token.empty())
        return {401, {{"error", "Failed to exchange code with Twitch"}}};

    auto twitch_user = twitch_.get_twitch_user(token_set->access_token);
    if (!twitch_user)
        return {401, {{"error", "Failed to get Twitch user info"}}};

    std::cout << "[Auth] Twitch user: " << twitch_user->display_name
              << " (id: " << twitch_user->id << ")\n";

    auto result = auth_.login_twitch(
        twitch_user->id, twitch_user->display_name,
        twitch_user->email, twitch_user->profile_image_url);

    if (!result)
        return {500, {{"error", "Failed to create or update user"}}};

    const bool consent_current =
        !result->user.legal_consent_version.empty()
        && result->user.legal_consent_version == kLegalConsentVersion;

    if (!consent_current) {
        if (!has_consent)
            return {400, {{"error", "Требуется согласие с документами (поле consent)"}}};
        if (!auth_.record_legal_consent(result->user.id, cver, accepted_at))
            return {500, {{"error", "Failed to save legal consent"}}};
        result->user.legal_consent_version = cver;
        result->user.legal_consent_at = accepted_at;
    }

    auth_.save_twitch_oauth(result->user.id, token_set->access_token, token_set->refresh_token,
                            now_plus_seconds_iso(std::max(60, token_set->expires_in - 60)));

    return {200, {{"token", result->token}, {"user", result->user}}};
}

} // namespace clipstream::controllers
