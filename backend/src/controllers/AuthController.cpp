#include "controllers/AuthController.hpp"
#include "utils/Crypto.hpp"
#include "utils/RuntimeConfig.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sstream>

namespace clipstream::controllers {

namespace {
std::string legal_version() { return utils::legal_consent_version(); }

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

AuthController::AuthController(services::AuthService &auth_service, services::TwitchService &twitch_service,
                               services::CacheService &cache_service, const std::string &redirect_uri,
                               repositories::AccountSecurityRepository &security)
    : auth_(auth_service), twitch_(twitch_service), cache_(cache_service), redirect_uri_(redirect_uri),
      security_(security) {}

void AuthController::register_routes(server::Router &router) {
    router.post("/auth/register", [this](const server::Request &r) { return handle_register(r); });
    router.post("/auth/logout", [this](const server::Request &r) -> server::Response {
        if (!r.user_id)
            return {401, {{"error", "Unauthorized"}}};
        auth_.revoke_sessions(r.user_id);
        return {200, {{"ok", true}}};
    });
    router.post("/auth/login", [this](const server::Request &r) { return handle_login(r); });
    router.get("/auth/me", [this](const server::Request &r) { return handle_me(r); });
    router.get("/auth/twitch/url", [this](const server::Request &r) { return handle_twitch_url(r); });
    router.post("/auth/twitch", [this](const server::Request &r) { return handle_twitch_login(r); });
}

server::Response AuthController::handle_register(const server::Request &req) {
    (void)req;
    return {410, {{"error", "Password registration is not supported; use Twitch OAuth"}}};
}

server::Response AuthController::handle_login(const server::Request &req) {
    (void)req;
    return {410, {{"error", "Password sign-in is not supported; use Twitch OAuth"}}};
}

server::Response AuthController::handle_me(const server::Request &req) {
    if (req.user_id == 0)
        return {401, {{"error", "Unauthorized"}}};
    auto profile = auth_.get_user_profile(req.user_id);
    if (profile.contains("error"))
        return {404, {{"error", "User not found"}}};
    return {200, profile};
}

std::string AuthController::generate_state() {
    unsigned char buf[16];
    if (RAND_bytes(buf, sizeof(buf)) != 1)
        throw std::runtime_error("Random generator unavailable");
    std::ostringstream oss;
    for (auto b : buf)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return oss.str();
}

server::Response AuthController::handle_twitch_url(const server::Request &req) {
    if (!utils::runtime_config()["auth"]["twitch_enabled"].get<bool>())
        return {503, {{"error", "Twitch sign-in is not configured"}}};
    if (!cache_.acquire_rate_slot("oauth:start:" + req.client_address, 30, 60))
        return {429, {{"error", "Too many sign-in attempts"}, {"retry_after", 60}}};
    auto challenge = req.query.find("challenge");
    unsigned char decoded[32];
    if (challenge == req.query.end() || !utils::unhex(challenge->second, decoded, 32))
        return {400, {{"error", "SHA256 browser challenge required"}}};
    auto state = generate_state();
    cache_.set_required("oauth_state:" + state, challenge->second, 300);

    auto url = twitch_.get_auth_url(redirect_uri_) + "&state=" + state;
    return {200, {{"url", url}, {"state", state}}};
}

server::Response AuthController::handle_twitch_login(const server::Request &req) {
    if (!utils::runtime_config()["auth"]["twitch_enabled"].get<bool>())
        return {503, {{"error", "Twitch sign-in is not configured"}}};
    auto &b = req.body;
    if (!b.contains("consent") || !b["consent"].is_object() ||
        b["consent"].value("version", std::string{}) != legal_version())
        return {400, {{"error", "Current legal consent required before Twitch sign-in"}}};
    if (!b.contains("code") || !b.contains("state"))
        return {400, {{"error", "Missing fields: code, state"}}};

    bool has_consent = b.contains("consent") && b["consent"].is_object();
    std::string cver;
    std::string accepted_at;
    if (has_consent) {
        const auto &consent = b["consent"];
        cver = consent.value("version", std::string{});
        accepted_at = now_plus_seconds_iso(0);
        if (cver != legal_version() || accepted_at.empty())
            return {400, {{"error", "Current legal consent required"}}};
    }

    std::string code = b["code"];
    std::string state = b["state"];

    const std::string verifier = b.value("verifier", std::string{});
    if (state.size() != 32 || verifier.size() < 43 || verifier.size() > 128 || code.size() > 2048)
        return {400, {{"error", "Invalid OAuth callback"}}};
    unsigned char proof[32];
    SHA256(reinterpret_cast<const unsigned char *>(verifier.data()), verifier.size(), proof);
    auto challenge = utils::hex(proof, 32);
    auto ok = cache_.consume("oauth_state:" + state);
    if (!ok || ok->size() != challenge.size() ||
        CRYPTO_memcmp(ok->data(), challenge.data(), challenge.size()) != 0)
        return {401, {{"error", "Invalid or expired browser state"}}};

    std::cout << "[Auth] Twitch OAuth exchange for code...\n";

    auto token_set = twitch_.exchange_code_tokens(code, redirect_uri_);
    if (!token_set || token_set->access_token.empty())
        return {401, {{"error", "Failed to exchange code with Twitch"}}};

    auto twitch_user = twitch_.get_twitch_user(token_set->access_token);
    if (!twitch_user)
        return {401, {{"error", "Failed to get Twitch user info"}}};

    std::cout << "[Auth] Twitch user: " << twitch_user->display_name << " (id: " << twitch_user->id << ")\n";

    auto result = auth_.login_twitch(twitch_user->id, twitch_user->login, twitch_user->email,
                                     twitch_user->profile_image_url);

    if (!result)
        return {409,
                {{"error", "Учётная запись Twitch конфликтует с существующими данными. Обратитесь к "
                           "оператору сервиса для проверки привязки"}}};

    const bool consent_current =
        !result->user.legal_consent_version.empty() && result->user.legal_consent_version == legal_version();

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

    auth_.user_access_token(result->user.id);
    if (security_.bootstrap_pending()) {
        auto canonical = twitch_.fetch_user_by_login("deexsd");
        if (canonical && security_.bootstrap_admin(result->user.id, canonical->id)) {
            result->user = *auth_.get_user(result->user.id);
            result->token = auth_.renew_token(result->user.id);
        }
    }
    return {200, {{"token", result->token}, {"user", result->user}}};
}

} // namespace clipstream::controllers
