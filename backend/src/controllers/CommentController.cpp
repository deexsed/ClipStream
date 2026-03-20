#include "controllers/CommentController.hpp"
#include "models/User.hpp"

namespace clipstream::controllers {

namespace {

void enrich_comment(models::Comment& c, const std::string& clip_broadcaster_id) {
    c.is_clip_channel_owner = !c.author_twitch_id.empty() && !clip_broadcaster_id.empty()
        && c.author_twitch_id == clip_broadcaster_id;
    c.author_badges.clear();
    if (c.author_role == "admin") c.author_badges.push_back("admin");
    else if (c.author_role == "moderator") c.author_badges.push_back("moderator");
    else if (c.author_role == "vip") c.author_badges.push_back("vip");
    if (c.is_clip_channel_owner) c.author_badges.push_back("broadcaster");
    if (!c.author_cosmetic_badge.empty() && c.author_cosmetic_badge != "none")
        c.author_badges.push_back(c.author_cosmetic_badge);
}

void enrich_tree(std::vector<models::Comment>& list, const std::string& clip_broadcaster_id) {
    for (auto& c : list) {
        enrich_comment(c, clip_broadcaster_id);
        if (!c.replies.empty()) enrich_tree(c.replies, clip_broadcaster_id);
    }
}

} // namespace

CommentController::CommentController(repositories::CommentRepository& repo, services::AuthService& auth,
                                     repositories::ClipRepository& clips, repositories::UserRepository& users)
    : repo_(repo), auth_(auth), clips_(clips), users_(users) {}

void CommentController::register_routes(server::Router& router) {
    router.get("/clips/:clip_id/comments",  [this](const server::Request& r) { return handle_get(r); });
    router.post("/clips/:clip_id/comments", [this](const server::Request& r) { return handle_create(r); });
    router.put("/comments/:id",            [this](const server::Request& r) { return handle_update(r); });
    router.del("/comments/:id",             [this](const server::Request& r) { return handle_delete(r); });
}

server::Response CommentController::handle_get(const server::Request& req) {
    uint64_t clip_id = std::stoull(req.params.at("clip_id"));

    uint32_t limit = 20, offset = 0;
    auto it = req.query.find("limit");
    if (it != req.query.end()) limit = static_cast<uint32_t>(std::stoul(it->second));
    it = req.query.find("offset");
    if (it != req.query.end()) offset = static_cast<uint32_t>(std::stoul(it->second));

    std::string broadcaster_id;
    if (auto clip = clips_.find_by_id(clip_id)) broadcaster_id = clip->broadcaster_id;

    auto comments = repo_.find_by_clip(clip_id, limit, offset);
    enrich_tree(comments, broadcaster_id);

    nlohmann::json j = nlohmann::json::array();
    for (auto& c : comments) j.push_back(c);

    return {200, {{"comments", j}}};
}

server::Response CommentController::handle_create(const server::Request& req) {
    if (req.user_id == 0) return {401, {{"error", "Unauthorized"}}};

    if (!users_.may_post_comment(req.user_id))
        return {403, {{"error", "Commenting is not allowed (account or chat restriction)"}}};

    uint64_t clip_id = std::stoull(req.params.at("clip_id"));
    if (!req.body.contains("body")) return {400, {{"error", "Missing 'body' field"}}};

    models::Comment comment;
    comment.clip_id = clip_id;
    comment.user_id = req.user_id;
    comment.body    = req.body["body"];

    if (req.body.contains("parent_id"))
        comment.parent_id = req.body["parent_id"].get<uint64_t>();

    uint64_t id = repo_.create(comment);
    auto created = repo_.find_by_id(id);
    if (!created) return {500, {{"error", "Failed to create comment"}}};

    std::string broadcaster_id;
    if (auto clip = clips_.find_by_id(clip_id)) broadcaster_id = clip->broadcaster_id;
    enrich_comment(*created, broadcaster_id);

    return {201, *created};
}

server::Response CommentController::handle_update(const server::Request& req) {
    if (req.user_id == 0) return {401, {{"error", "Unauthorized"}}};

    uint64_t id = std::stoull(req.params.at("id"));
    if (!req.body.contains("body")) return {400, {{"error", "Missing 'body' field"}}};

    models::Comment comment;
    comment.id      = id;
    comment.user_id = req.user_id;
    comment.body    = req.body["body"];

    if (!repo_.update(comment)) return {403, {{"error", "Cannot update this comment"}}};
    return {200, {{"ok", true}}};
}

server::Response CommentController::handle_delete(const server::Request& req) {
    if (req.user_id == 0) return {401, {{"error", "Unauthorized"}}};

    uint64_t id = std::stoull(req.params.at("id"));

    auto comment = repo_.find_by_id(id);
    if (!comment) return {404, {{"error", "Comment not found"}}};

    const bool is_owner = comment->user_id == req.user_id;
    bool       is_staff = false;
    if (auto u = auth_.get_user(req.user_id)) {
        is_staff = u->role == models::UserRole::Moderator || u->role == models::UserRole::Admin;
    }
    if (!is_owner && !is_staff) return {403, {{"error", "Forbidden"}}};

    repo_.remove(id);
    return {200, {{"ok", true}}};
}

} // namespace clipstream::controllers
