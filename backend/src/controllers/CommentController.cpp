#include "controllers/CommentController.hpp"
#include "models/User.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

void collect_user_ids_recursive(models::Comment& c, std::unordered_set<uint64_t>& seen, std::vector<uint64_t>& out) {
    if (seen.insert(c.user_id).second) out.push_back(c.user_id);
    for (auto& r : c.replies) collect_user_ids_recursive(r, seen, out);
}

void collect_user_ids(std::vector<models::Comment>& list, std::vector<uint64_t>& out) {
    std::unordered_set<uint64_t> seen;
    for (auto& c : list) collect_user_ids_recursive(c, seen, out);
}

void apply_friendship_recursive(models::Comment& c, uint64_t viewer_id,
                                const std::unordered_map<uint64_t, std::string>& st) {
    if (viewer_id == 0)
        c.viewer_friendship = "none";
    else if (c.user_id == viewer_id)
        c.viewer_friendship = "self";
    else {
        auto it = st.find(c.user_id);
        c.viewer_friendship = it == st.end() ? "none" : it->second;
    }
    for (auto& r : c.replies) apply_friendship_recursive(r, viewer_id, st);
}

void apply_friendship_states(std::vector<models::Comment>& list, uint64_t viewer_id,
                             const std::unordered_map<uint64_t, std::string>& st) {
    for (auto& c : list) apply_friendship_recursive(c, viewer_id, st);
}

} // namespace

CommentController::CommentController(repositories::CommentRepository& repo, services::AuthService& auth,
                                     repositories::ClipRepository& clips, repositories::UserRepository& users,
                                     repositories::NotificationRepository& notifications)
    : repo_(repo), auth_(auth), clips_(clips), users_(users), notifications_(notifications) {}

void CommentController::register_routes(server::Router& router) {
    router.get("/clips/:clip_id/comments",   [this](const server::Request& r) { return handle_get(r); });
    router.post("/clips/:clip_id/comments",  [this](const server::Request& r) { return handle_create(r); });
    router.put("/comments/:id",              [this](const server::Request& r) { return handle_update(r); });
    router.del("/comments/:id",              [this](const server::Request& r) { return handle_delete(r); });
    router.post("/comments/:id/vote",        [this](const server::Request& r) { return handle_vote(r); });
}

server::Response CommentController::handle_get(const server::Request& req) {
    uint64_t clip_id = std::stoull(req.params.at("clip_id"));

    uint32_t limit = 20, offset = 0;
    auto it = req.query.find("limit");
    if (it != req.query.end()) limit = static_cast<uint32_t>(std::stoul(it->second));
    it = req.query.find("offset");
    if (it != req.query.end()) offset = static_cast<uint32_t>(std::stoul(it->second));

    std::string sort = "recent";
    it = req.query.find("sort");
    if (it != req.query.end() && it->second == "best") sort = "best";

    std::string broadcaster_id;
    if (auto clip = clips_.find_by_id(clip_id)) broadcaster_id = clip->broadcaster_id;

    auto comments = repo_.find_by_clip(clip_id, limit, offset, sort, req.user_id);
    enrich_tree(comments, broadcaster_id);

    std::vector<uint64_t> uid_list;
    collect_user_ids(comments, uid_list);
    std::unordered_map<uint64_t, std::string> fstate;
    if (req.user_id != 0 && !uid_list.empty()) fstate = users_.friendship_status_bulk(req.user_id, uid_list);
    apply_friendship_states(comments, req.user_id, fstate);

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

    std::optional<uint64_t> parent_id_opt;
    if (req.body.contains("parent_id"))
        parent_id_opt = req.body["parent_id"].get<uint64_t>();
    comment.parent_id = parent_id_opt;

    uint64_t id = repo_.create(comment);
    auto created = repo_.find_by_id(id);
    if (!created) return {500, {{"error", "Failed to create comment"}}};

    if (!parent_id_opt) {
        if (auto clip = clips_.find_by_id(clip_id)) {
            const uint64_t owner = users_.clip_owner_user_id_for_reputation(*clip);
            if (owner != 0 && owner != req.user_id) users_.update_reputation(owner, 3);
        }
    }

    if (parent_id_opt) {
        auto parent = repo_.find_by_id(*parent_id_opt);
        if (parent && parent->user_id != req.user_id && parent->clip_id == clip_id) {
            std::string snippet = comment.body;
            if (snippet.size() > 120) snippet.resize(120);
            notifications_.notify_comment_reply(parent->user_id, req.user_id, clip_id, *parent_id_opt, id, snippet);
        }
    }

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

    if (!comment->parent_id) {
        if (auto clip = clips_.find_by_id(comment->clip_id)) {
            const uint64_t owner = users_.clip_owner_user_id_for_reputation(*clip);
            if (owner != 0 && owner != comment->user_id) users_.update_reputation(owner, -3);
        }
    }

    repo_.remove(id);
    return {200, {{"ok", true}}};
}

server::Response CommentController::handle_vote(const server::Request& req) {
    if (req.user_id == 0) return {401, {{"error", "Unauthorized"}}};
    uint64_t id = std::stoull(req.params.at("id"));
    if (!req.body.contains("value")) return {400, {{"error", "Missing value"}}};
    int value = 0;
    try {
        value = req.body["value"].get<int>();
    } catch (...) {
        return {400, {{"error", "Invalid value"}}};
    }
    if (value != -1 && value != 0 && value != 1) return {400, {{"error", "value must be -1, 0, or 1"}}};
    if (!repo_.find_by_id(id)) return {404, {{"error", "Comment not found"}}};
    if (!repo_.set_comment_vote(req.user_id, id, value)) return {500, {{"error", "Vote failed"}}};
    return {200, {{"ok", true}}};
}

} // namespace clipstream::controllers
