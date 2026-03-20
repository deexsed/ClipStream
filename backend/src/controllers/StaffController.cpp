#include "controllers/StaffController.hpp"
#include "models/User.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace clipstream::controllers {

namespace {

const auto g_boot_steady = std::chrono::steady_clock::now();

std::string utc_now_iso_rfc3339() {
    auto   tp = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm     tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

bool is_staff(models::UserRole r) {
    return r == models::UserRole::Moderator || r == models::UserRole::Admin;
}

const char* role_string(models::UserRole r) {
    switch (r) {
        case models::UserRole::Moderator: return "moderator";
        case models::UserRole::Admin: return "admin";
        case models::UserRole::Vip: return "vip";
        default: return "user";
    }
}

server::Response staff_forbidden() { return {403, {{"error", "Staff access required"}}}; }

server::Response staff_unauthorized() { return {401, {{"error", "Unauthorized"}}}; }

} // namespace

StaffController::StaffController(services::AuthService& auth, services::CacheService& cache,
                                   repositories::ClipRepository& clip_repo)
    : auth_(auth), cache_(cache), clip_repo_(clip_repo) {}

void StaffController::register_routes(server::Router& router) {
    router.get("/staff/bootstrap", [this](const server::Request& r) { return handle_bootstrap(r); });
    router.get("/staff/diagnostics", [this](const server::Request& r) { return handle_diagnostics(r); });
    router.get("/staff/content-reports", [this](const server::Request& r) { return handle_content_reports(r); });
}

server::Response StaffController::handle_bootstrap(const server::Request& req) {
    if (req.user_id == 0) return staff_unauthorized();
    auto user = auth_.get_user(req.user_id);
    if (!user || !is_staff(user->role)) return staff_forbidden();

    nlohmann::json caps = nlohmann::json::object();
    const bool     admin = user->role == models::UserRole::Admin;
    caps["diagnostics"] = true;
    caps["analytics"] = admin;
    caps["admin_settings"] = admin;
    caps["moderation"] = user->role == models::UserRole::Moderator || admin;

    return {200,
            {{"role", role_string(user->role)},
             {"role_level", static_cast<int>(user->role)},
             {"user_id", user->id},
             {"username", user->username},
             {"version", "0.2.0-dev"},
             {"server_time_utc", utc_now_iso_rfc3339()},
             {"capabilities", caps}}};
}

server::Response StaffController::handle_diagnostics(const server::Request& req) {
    if (req.user_id == 0) return staff_unauthorized();
    auto user = auth_.get_user(req.user_id);
    if (!user || !is_staff(user->role)) return staff_forbidden();

    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - g_boot_steady)
                            .count();

    nlohmann::json body = {{"mysql_ok", clip_repo_.ping_db()},
                           {"redis_ok", cache_.ping_redis()},
                           {"uptime_seconds", uptime},
                           {"utc_time", utc_now_iso_rfc3339()},
                           {"process", {{"version", "0.2.0-dev"}}}};
    return {200, body};
}

server::Response StaffController::handle_content_reports(const server::Request& req) {
    if (req.user_id == 0) return staff_unauthorized();
    auto user = auth_.get_user(req.user_id);
    if (!user || !is_staff(user->role)) return staff_forbidden();

    uint32_t limit = 50, offset = 0;
    auto lit = req.query.find("limit");
    if (lit != req.query.end()) {
        try {
            limit = static_cast<uint32_t>(std::stoul(lit->second));
        } catch (...) {}
    }
    auto oit = req.query.find("offset");
    if (oit != req.query.end()) {
        try {
            offset = static_cast<uint32_t>(std::stoul(oit->second));
        } catch (...) {}
    }
    if (limit > 200) limit = 200;
    auto reports = clip_repo_.list_content_reports(limit, offset);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : reports) arr.push_back(r);
    return {200, {{"reports", arr}}};
}

} // namespace clipstream::controllers
