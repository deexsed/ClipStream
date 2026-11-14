#include "controllers/AdsController.hpp"
#include "utils/AdValidation.hpp"
#include "utils/Environment.hpp"
#include "utils/Crypto.hpp"
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace clipstream::controllers {
namespace {
std::string digest(const std::string &s) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(s.data()),s.size(),hash);
    return utils::hex(hash,sizeof hash);
}
std::string random_token() {
    unsigned char bytes[32];
    if (RAND_bytes(bytes,sizeof bytes) != 1) throw std::runtime_error("Secure advertisement token unavailable");
    return utils::hex(bytes,sizeof bytes);
}
bool token_valid(const std::string &s) {
    return s.size() == 64 && std::all_of(s.begin(),s.end(),[](char c) { return (c>='0'&&c<='9') || (c>='a'&&c<='f'); });
}
} // namespace
AdsController::AdsController(services::AuthService &auth, repositories::AdsRepository &ads, const std::string &secret)
    : auth_(auth), ads_(ads), secret_(secret), hosts_(utils::ad_media_hosts(utils::env("ADS_MEDIA_HOSTS"))) {
    if (secret_.size() < 32) throw std::invalid_argument("Advertisement audience key too short");
}
bool AdsController::admin(const server::Request &req) {
    if (!req.user_id) return false;
    const auto actor = auth_.get_user(req.user_id);
    return actor && !actor->account_blocked && actor->role == models::UserRole::Admin;
}
void AdsController::register_routes(server::Router &r) {
    r.get("/admin/ads",[this](const auto &q) { return list(q); });
    r.get("/admin/ads/metrics",[this](const server::Request &q) -> server::Response {
        if (!admin(q)) return {q.user_id ? 403u : 401u,{{"error","Administrator access required"}}};
        return {200,ads_.metrics()};
    });
    r.get("/admin/ads/:id",[this](const auto &q) { return find(q); });
    r.post("/admin/ads",[this](const auto &q) { return save(q,false); });
    r.put("/admin/ads/:id",[this](const auto &q) { return save(q,true); });
    r.del("/admin/ads/:id",[this](const auto &q) { return remove(q); });
    r.get("/ads/serve",[this](const auto &q) { return serve(q); });
    r.post("/ads/events",[this](const auto &q) { return event(q); });
}
server::Response AdsController::list(const server::Request &q) {
    if (!admin(q)) return {q.user_id ? 403u : 401u,{{"error","Administrator access required"}}};
    const auto limit = q.query.contains("limit") ? std::stoul(q.query.at("limit")) : 30;
    const auto offset = q.query.contains("offset") ? std::stoul(q.query.at("offset")) : 0;
    return {200,ads_.list(limit,offset)};
}
server::Response AdsController::find(const server::Request &q) {
    if (!admin(q)) return {q.user_id ? 403u : 401u,{{"error","Administrator access required"}}};
    const auto found = ads_.find(std::stoull(q.params.at("id")));
    return found ? server::Response{200,*found} : server::Response{404,{{"error","Campaign not found"}}};
}
server::Response AdsController::save(const server::Request &q, bool update) {
    if (!admin(q)) return {q.user_id ? 403u : 401u,{{"error","Administrator access required"}}};
    const auto value = utils::validate_ad(q.body,hosts_);
    uint64_t id;
    if (update) {
        id = std::stoull(q.params.at("id"));
        if (!ads_.update(id,value)) return {404,{{"error","Campaign not found"}}};
    } else id = ads_.create(value);
    const auto saved = ads_.find(id);
    return saved ? server::Response{update ? 200u : 201u,{{"campaign",*saved}}} : server::Response{404,{{"error","Campaign was concurrently removed"}}};
}
server::Response AdsController::remove(const server::Request &q) {
    if (!admin(q)) return {q.user_id ? 403u : 401u,{{"error","Administrator access required"}}};
    return ads_.remove(std::stoull(q.params.at("id"))) ? server::Response{200,{{"deleted",true}}} : server::Response{404,{{"error","Campaign not found"}}};
}
server::Response AdsController::serve(const server::Request &q) {
    const auto placement = q.query.contains("placement") ? q.query.at("placement") : "";
    if (placement != "banner" && placement != "feed" && placement != "preroll") return {400,{{"error","Invalid advertisement placement"}}};
    const auto session = q.query.contains("session_id") ? q.query.at("session_id") : "";
    if (!q.user_id && (session.size() < 16 || session.size() > 128 || !std::all_of(session.begin(),session.end(),[](unsigned char c) { return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'; })))
        return {400,{{"error","A browser session_id of 16-128 ASCII letters, digits, underscore or hyphen is required"}}};
    const auto identity = q.user_id ? "user:" + std::to_string(q.user_id) : "session:" + session;
    unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned size = 0;
    if (!HMAC(EVP_sha256(),secret_.data(),int(secret_.size()),reinterpret_cast<const unsigned char *>(identity.data()),identity.size(),bytes,&size))
        throw std::runtime_error("Advertisement audience key unavailable");
    const auto proof = random_token();
    auto result = ads_.serve(placement,utils::hex(bytes,size),digest(proof),hosts_);
    if (!result["ad"].is_null()) {
        result["serve_token"] = proof;
    }
    return {200,result};
}
server::Response AdsController::event(const server::Request &q) {
    if (!q.body.is_object() || !q.body.contains("serve_token") || !q.body["serve_token"].is_string() || !q.body.contains("event") || !q.body["event"].is_string())
        return {400,{{"error","serve_token and event strings are required"}}};
    const auto proof = q.body["serve_token"].get<std::string>(), kind = q.body["event"].get<std::string>();
    if (!token_valid(proof) || (kind != "impression" && kind != "click")) return {400,{{"error","Invalid advertisement event"}}};
    const auto recorded = ads_.event(digest(proof),kind);
    return recorded ? server::Response{200,*recorded} : server::Response{404,{{"error","Advertisement proof not found or expired"}}};
}
} // namespace clipstream::controllers
