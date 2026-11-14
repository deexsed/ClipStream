#include "services/ClipService.hpp"
#include "services/RecommendationScoring.hpp"
#include "utils/Crypto.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_set>

namespace clipstream::services {

using json = nlohmann::json;

namespace {

const repositories::FeedMuteSet *resolve_mutes(repositories::ClipRepository &repo, uint64_t user_id,
                                               const std::optional<std::vector<std::string>> &clip_langs_q,
                                               repositories::FeedMuteSet &storage) {
    if (user_id != 0) {
        repo.load_feed_mutes(user_id, storage);
        if (clip_langs_q.has_value())
            storage.only_broadcast_langs = *clip_langs_q;
        return &storage;
    }
    if (!clip_langs_q.has_value())
        return nullptr;
    storage = {};
    storage.only_broadcast_langs = *clip_langs_q;
    return &storage;
}

} // namespace

ClipService::ClipService(repositories::ClipRepository &clip_repo, repositories::UserRepository &user_repo,
                         CacheService &cache)
    : clip_repo_(clip_repo), user_repo_(user_repo), cache_(cache) {}

std::string ClipService::cache_key(const repositories::ClipFilter &f, uint32_t limit, uint32_t offset,
                                   uint64_t user_id) {
    const auto identity = json::array({f.game_id, static_cast<int>(f.sort), static_cast<int>(f.period), f.tag,
                                       f.broadcaster_name, f.search, limit, offset, user_id})
                              .dump();
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(identity.data()), identity.size(), digest);
    return "clips:" + utils::hex(digest, sizeof digest);
}

static models::Clip clip_from_json(const json &item) {
    models::Clip c;
    c.id = item["id"];
    c.twitch_clip_id = item.value("twitch_clip_id", "");
    c.title = item.value("title", "");
    c.thumbnail_url = item.value("thumbnail_url", "");
    c.embed_url = item.value("embed_url", "");
    c.duration = item.value("duration", 0.0f);
    c.view_count = item.value("view_count", 0u);
    c.like_count = item.value("like_count", 0);
    c.dislike_count = item.value("dislike_count", 0);
    if (item.contains("comment_count") && item["comment_count"].is_number_unsigned())
        c.comment_count = item["comment_count"].get<uint64_t>();
    if (item.contains("platform_view_count") && item["platform_view_count"].is_number_unsigned())
        c.platform_view_count = item["platform_view_count"].get<uint64_t>();
    c.broadcaster_name = item.value("broadcaster_name", "");
    c.broadcaster_login = item.value("broadcaster_login", "");
    c.broadcaster_id = item.value("broadcaster_id", "");
    c.broadcaster_avatar = item.value("broadcaster_avatar", "");
    c.game_id = item.value("game_id", 0ULL);
    c.game_name = item.value("game_name", "");
    c.created_at = item.value("created_at", "");
    c.vod_id = item.value("vod_id", "");
    c.vod_offset = item.value("vod_offset", 0);
    c.twitch_created_at = item.value("twitch_created_at", "");
    c.broadcaster_lang = item.value("language", std::string{});
    c.is_mature = item.value("is_mature", false);
    c.classification_source = item.value("classification_source", std::string("unknown"));
    if (item.contains("tags"))
        c.tags = item["tags"].get<std::vector<std::string>>();
    if (item.contains("promo_label") && item["promo_label"].is_string())
        c.promo_label = item["promo_label"].get<std::string>();
    c.creator_twitch_id = item.value("creator_twitch_id", "");
    c.creator_login = item.value("creator_login", "");
    return c;
}

std::vector<models::Clip> ClipService::get_feed(const repositories::ClipFilter &filter, uint32_t limit,
                                                uint32_t offset, uint64_t user_id,
                                                const std::optional<std::vector<std::string>> &clip_langs_q) {
    std::string key = cache_key(filter, limit, offset, user_id);
    if (clip_langs_q.has_value()) {
        const auto languages = json(*clip_langs_q).dump();
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char *>(languages.data()), languages.size(), digest);
        key += ":cl:" + utils::hex(digest, sizeof digest);
    }
    auto cached = cache_.get(key);
    if (cached) {
        auto j = json::parse(*cached);
        std::vector<models::Clip> clips;
        for (auto &item : j)
            clips.push_back(clip_from_json(item));
        std::vector<models::Clip *> vote_targets;
        for (auto &clip : clips)
            vote_targets.push_back(&clip);
        clip_repo_.fill_user_votes(vote_targets, user_id);
        return clips;
    }

    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mp = resolve_mutes(clip_repo_, user_id, clip_langs_q, mutes);

    auto clips = clip_repo_.find_filtered(filter, limit, offset, mp);
    std::vector<models::Clip *> count_targets;
    for (auto &clip : clips)
        count_targets.push_back(&clip);
    clip_repo_.fill_card_counts(count_targets);
    json j = json::array();
    for (auto &c : clips)
        j.push_back(c);
    cache_.set(key, j.dump(), 60);
    clip_repo_.fill_user_votes(count_targets, user_id);
    return clips;
}

std::vector<models::Clip>
ClipService::get_personalized_feed(const repositories::ClipFilter &filter,
                                   const std::vector<std::string> &followed_broadcasters, uint32_t limit,
                                   uint32_t offset, uint64_t user_id,
                                   const std::optional<std::vector<std::string>> &clip_langs_q) {
    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mp = resolve_mutes(clip_repo_, user_id, clip_langs_q, mutes);

    if (followed_broadcasters.empty())
        return get_feed(filter, limit, offset, user_id, clip_langs_q);
    auto personalized = clip_repo_.find_personalized(filter, followed_broadcasters, limit, offset, mp);
    if (!personalized.empty())
        return personalized;
    return get_feed(filter, limit, offset, user_id, clip_langs_q);
}

std::optional<models::Clip> ClipService::get_clip(uint64_t id) {
    auto cached = cache_.get("clip:" + std::to_string(id));
    if (cached)
        return clip_from_json(json::parse(*cached));

    auto clip = clip_repo_.find_by_id(id);
    if (clip) {
        json j = *clip;
        cache_.set("clip:" + std::to_string(id), j.dump(), 300);
    }
    return clip;
}

std::optional<nlohmann::json> ClipService::get_clip_json_for_viewer(uint64_t clip_id,
                                                                    uint64_t viewer_user_id) {
    auto clip = clip_repo_.find_by_id(clip_id);
    if (!clip)
        return std::nullopt;
    nlohmann::json j = *clip;
    if (viewer_user_id != 0) {
        auto v = clip_repo_.get_user_vote(viewer_user_id, clip_id);
        if (!v.has_value())
            j["user_vote"] = nullptr;
        else
            j["user_vote"] = v.value() ? "like" : "dislike";
    } else {
        j["user_vote"] = nullptr;
    }
    if (!clip->creator_twitch_id.empty()) {
        if (auto site = clip_repo_.find_user_brief_by_twitch_id(clip->creator_twitch_id))
            j["creator_site"] = *site;
    }
    return j;
}

std::optional<nlohmann::json> ClipService::get_clip_trend_ranks(uint64_t clip_id, uint64_t viewer_user_id) {
    constexpr uint32_t k_cap = 50;
    auto clip = clip_repo_.find_by_id(clip_id);
    if (!clip)
        return std::nullopt;

    auto clip_langs_world = std::make_optional(std::vector<std::string>{});
    repositories::FeedMuteSet mutes_world;
    const repositories::FeedMuteSet *mp_world =
        resolve_mutes(clip_repo_, viewer_user_id, clip_langs_world, mutes_world);

    auto normalize_lang = [](const std::string &raw) {
        std::string norm;
        for (char ch : raw) {
            if (ch >= 'A' && ch <= 'Z')
                norm += static_cast<char>(ch - 'A' + 'a');
            else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')
                norm += ch;
            if (norm.size() >= 16)
                break;
        }
        return norm;
    };

    nlohmann::json periods = nlohmann::json::object();
    const repositories::TimePeriod ps[] = {
        repositories::TimePeriod::Today,
        repositories::TimePeriod::Week,
        repositories::TimePeriod::Month,
        repositories::TimePeriod::All,
    };
    const char *keys[] = {"today", "week", "month", "all"};

    for (size_t i = 0; i < 4; ++i) {
        nlohmann::json slot = nlohmann::json::object();
        if (auto w = clip_repo_.popular_feed_position(clip_id, ps[i], mp_world, k_cap))
            slot["world"] = *w;

        const std::string lang_norm = normalize_lang(clip->broadcaster_lang);
        if (!lang_norm.empty()) {
            repositories::FeedMuteSet mutes_lang = mutes_world;
            mutes_lang.only_broadcast_langs = {lang_norm};
            if (auto l = clip_repo_.popular_feed_position(clip_id, ps[i], &mutes_lang, k_cap))
                slot["lang"] = *l;
        }
        if (!slot.empty())
            periods[keys[i]] = std::move(slot);
    }

    nlohmann::json out = nlohmann::json::object();
    out["periods"] = std::move(periods);
    if (!clip->broadcaster_lang.empty())
        out["clip_language"] = clip->broadcaster_lang;
    return out;
}

std::vector<models::Clip> ClipService::search(const std::string &query, uint32_t limit, uint32_t offset,
                                              uint64_t viewer_user_id,
                                              const std::optional<std::vector<std::string>> &clip_langs_q) {
    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mp = resolve_mutes(clip_repo_, viewer_user_id, clip_langs_q, mutes);
    return clip_repo_.search(query, limit, offset, mp);
}

std::vector<models::Clip>
ClipService::get_discussed_clips(uint32_t hours, uint32_t limit, uint64_t viewer_user_id,
                                 const std::optional<std::vector<std::string>> &clip_langs_q) {
    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mp = resolve_mutes(clip_repo_, viewer_user_id, clip_langs_q, mutes);
    return clip_repo_.find_discussed_clips(hours, limit, mp, true);
}

std::vector<models::Clip>
ClipService::get_continue_watching(uint32_t limit, uint64_t viewer_user_id,
                                   const std::optional<std::vector<std::string>> &clip_langs_q) {
    if (viewer_user_id == 0)
        return {};
    if (limit < 1)
        limit = 1;
    if (limit > 36)
        limit = 36;
    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mp = resolve_mutes(clip_repo_, viewer_user_id, clip_langs_q, mutes);
    return clip_repo_.find_continue_watching(viewer_user_id, limit, mp, true);
}

std::vector<models::Clip>
ClipService::get_channel_updates_for_viewer(uint32_t limit, uint64_t viewer_user_id,
                                            const std::optional<std::vector<std::string>> &clip_langs_q) {
    if (viewer_user_id == 0)
        return {};
    if (limit < 1)
        limit = 1;
    if (limit > 36)
        limit = 36;
    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mp = resolve_mutes(clip_repo_, viewer_user_id, clip_langs_q, mutes);
    return clip_repo_.find_clips_from_visited_channels(viewer_user_id, 21, 7, limit, mp, true);
}

std::optional<models::Clip>
ClipService::get_random_clip(uint64_t viewer_user_id,
                             const std::optional<std::vector<std::string>> &clip_langs_q) {
    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mp = resolve_mutes(clip_repo_, viewer_user_id, clip_langs_q, mutes);
    return clip_repo_.find_random_clip(mp, true);
}

nlohmann::json ClipService::get_home_sections(uint64_t user_id, uint32_t per_section,
                                              const std::vector<std::string> &tracked_broadcaster_ids,
                                              const std::string &session_id,
                                              const std::optional<std::vector<std::string>> &clip_langs_q) {
    (void)session_id;
    per_section = std::clamp(per_section, 4u, 24u);
    repositories::FeedMuteSet mutes;
    resolve_mutes(clip_repo_, user_id, clip_langs_q, mutes);
    auto profile = clip_repo_.recommendation_profile(user_id);
    profile.followed_channels.insert(profile.followed_channels.end(), tracked_broadcaster_ids.begin(),
                                     tracked_broadcaster_ids.end());
    auto candidates = clip_repo_.recommendation_candidates(user_id, &mutes, {});
    recommendations::score(candidates, profile);
    auto ranked = recommendations::select(std::move(candidates), per_section * 3);
    std::vector<models::Clip *> ptrs;
    for (auto &c : ranked)
        ptrs.push_back(&c.clip);
    clip_repo_.fill_tags_bulk(ptrs);
    clip_repo_.fill_card_counts(ptrs);
    clip_repo_.fill_user_votes(ptrs, user_id);
    json out = {{"hot", json::array()}, {"fresh", json::array()}, {"recommended", json::array()}};
    for (const auto &c : ranked) {
        std::string shelf = c.discovery ? "fresh" : c.evergreen ? "hot" : "recommended";
        if (out[shelf].size() >= per_section) {
            for (const auto *fallback : {"recommended", "fresh", "hot"})
                if (out[fallback].size() < per_section) {
                    shelf = fallback;
                    break;
                }
        }
        out[shelf].push_back(c.clip);
    }
    return out;
}

nlohmann::json ClipService::get_home_feed(uint64_t user_id, uint32_t limit, const std::string &cursor,
                                          const std::string &session_id,
                                          const std::optional<std::vector<std::string>> &clip_langs_q,
                                          const std::string &surface) {
    using namespace recommendations;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        limit = std::clamp(limit, 1u, 48u);
        if (session_id.size() > 64 || (surface != "home" && surface != "rush"))
            throw std::invalid_argument("Invalid recommendation session");
        repositories::FeedMuteSet mutes;
        resolve_mutes(clip_repo_, user_id, clip_langs_q, mutes);
        const std::string scope = json::array({user_id, surface, mutes.only_broadcast_langs}).dump();
        auto digest = [](const std::string &input) {
            unsigned char bytes[SHA256_DIGEST_LENGTH];
            SHA256(reinterpret_cast<const unsigned char *>(input.data()), input.size(), bytes);
            return utils::hex(bytes, 16);
        };
        auto valid_token = [](const std::string &s) {
            return s.size() == 32 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
        };
        std::string token;
        size_t offset = 0;
        bool persisted = true;
        bool from_cache = false;
        json queue;
        if (!cursor.empty()) {
            const auto colon = cursor.find(':');
            if (colon != 32 || cursor.size() > 40)
                throw std::invalid_argument("Invalid recommendation cursor");
            token = cursor.substr(0, colon);
            const auto tail = cursor.substr(colon + 1);
            const auto parsed = std::from_chars(tail.data(), tail.data() + tail.size(), offset);
            if (!valid_token(token) || tail.empty() || parsed.ec != std::errc{} ||
                parsed.ptr != tail.data() + tail.size() || offset > 200)
                throw std::invalid_argument("Invalid recommendation cursor");
            auto cached = cache_.get("reco:queue:" + token);
            if (!cached)
                throw std::invalid_argument("Recommendation cursor expired");
            queue = json::parse(*cached);
            if (queue.value("scope", "") != scope)
                throw std::invalid_argument("Recommendation cursor scope mismatch");
        } else {
            const auto initial_key = "reco:start:" + digest(scope + ":" + session_id);
            if (!session_id.empty()) {
                if (auto previous = cache_.get(initial_key); previous && valid_token(*previous)) {
                    if (auto cached = cache_.get("reco:queue:" + *previous)) {
                        token = *previous;
                        queue = json::parse(*cached);
                        from_cache = true;
                    } else
                        cache_.compare_delete(initial_key, *previous);
                }
            }
            if (queue.is_null()) {
                unsigned char bytes[16];
                if (RAND_bytes(bytes, sizeof bytes) != 1)
                    throw std::runtime_error("Random source unavailable");
                token = utils::hex(bytes, sizeof bytes);
                auto profile = clip_repo_.recommendation_profile(user_id);
                const auto policy = surface == "rush" ? Surface::Rush : Surface::Home;
                auto candidates =
                    clip_repo_.recommendation_candidates(user_id, &mutes, {}, {}, surface == "rush");
                score(candidates, profile, policy);
                uint64_t seed = 0;
                std::from_chars(token.data(), token.data() + 16, seed, 16);
                auto ranked = select(std::move(candidates), 200, seed, 0, policy);
                json ids = json::array(), sources = json::object();
                for (const auto &c : ranked) {
                    ids.push_back(c.clip.id);
                    sources[std::to_string(c.clip.id)] = c.discovery   ? "discovery"
                                                         : c.evergreen ? "evergreen"
                                                         : c.novel     ? "new_channel"
                                                                       : "for_you";
                }
                queue = {{"scope", scope}, {"ids", ids}, {"sources", sources}};
                // A cursor must never be issued for a queue that failed to persist.
                try {
                    cache_.set_required("reco:queue:" + token, queue.dump(), 1800);
                    if (!session_id.empty() && !cache_.try_set_nx(initial_key, token, 1800)) {
                        // The winner has persisted its queue before publishing the start pointer.
                        // Simultaneous first-page requests on different replicas use that same queue.
                        if (auto winner = cache_.get(initial_key); winner && valid_token(*winner)) {
                            if (auto cached = cache_.get("reco:queue:" + *winner)) {
                                token = *winner;
                                queue = json::parse(*cached);
                            }
                        }
                    }
                } catch (const sw::redis::Error &) {
                    persisted = false; // Serve a bounded SQL-backed page without an
                                       // unusable cursor.
                }
            }
        }
        const auto ids = queue["ids"].get<std::vector<uint64_t>>();
        if (offset > ids.size())
            throw std::invalid_argument("Recommendation cursor out of range");
        json rows = json::array();
        size_t next = offset;
        // Re-check all current hard filters and dislikes, even for an already-created
        // queue.
        if (offset < ids.size()) {
            std::vector<uint64_t> remaining(ids.begin() + offset, ids.end());
            auto allowed = clip_repo_.recommendation_candidates(user_id, &mutes, {}, remaining);
            std::unordered_map<uint64_t, models::Clip> by_id;
            for (auto &c : allowed)
                by_id.emplace(c.clip.id, std::move(c.clip));
            std::vector<models::Clip *> ptrs;
            while (next < ids.size() && ptrs.size() < limit) {
                auto it = by_id.find(ids[next++]);
                if (it != by_id.end())
                    ptrs.push_back(&it->second);
            }
            clip_repo_.fill_tags_bulk(ptrs);
            clip_repo_.fill_card_counts(ptrs);
            clip_repo_.fill_user_votes(ptrs, user_id);
            for (const auto *c : ptrs) {
                json clip = *c;
                clip["delivery_id"] = digest(token + ":" + std::to_string(c->id));
                if (auto it = std::find_if(allowed.begin(), allowed.end(),
                                           [&](const auto &x) { return x.clip.id == c->id; });
                    it != allowed.end() && it->recently_opened > 0)
                    clip["viewed"] = true;
                rows.push_back(
                    {{"shelf", queue["sources"][std::to_string(c->id)]}, {"clip", std::move(clip)}});
            }
        }
        if (from_cache && rows.empty() && cursor.empty() && attempt == 0) {
            // Deleted/moderated clips must not pin an otherwise valid session to an empty catalog.
            cache_.compare_delete("reco:start:" + digest(scope + ":" + session_id), token);
            continue;
        }
        clip_repo_.record_recommendation_delivery(rows, user_id, surface, static_cast<uint32_t>(offset));
        return {{"items", rows},
                {"count", rows.size()},
                {"next_cursor",
                 persisted && next < ids.size() ? json(token + ":" + std::to_string(next)) : json(nullptr)},
                {"algorithm_version", version},
                {"degraded", !persisted}};
    }
    throw std::logic_error("Recommendation refresh exhausted");
}

void ClipService::record_recommendation_impressions(uint64_t user_id,
                                                    const std::vector<std::string> &tokens) {
    if (tokens.size() > 48)
        throw std::invalid_argument("Too many impressions");
    for (const auto &s : tokens)
        if (s.size() != 32 || s.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::invalid_argument("Invalid delivery token");
    clip_repo_.record_recommendation_impressions(user_id, tokens);
}

void ClipService::save_recommendation_twitch_follows(uint64_t user_id, const nlohmann::json &channels) {
    std::vector<std::string> ids;
    for (const auto &ch : channels) {
        const auto id = ch.value("broadcaster_id", std::string{});
        if (!id.empty() && id.size() <= 64)
            ids.push_back(id);
    }
    clip_repo_.save_recommendation_twitch_follows(user_id, ids);
}

nlohmann::json ClipService::get_analytics_summary_24h() { return clip_repo_.get_analytics_summary_24h(); }

nlohmann::json ClipService::get_similar_streamers(uint64_t user_id, uint32_t limit) {
    if (user_id == 0)
        return {{"channels", json::array()}, {"count", 0}};

    repositories::FeedMuteSet mutes;
    resolve_mutes(clip_repo_, user_id, std::nullopt, mutes);
    auto candidates = clip_repo_.recommendation_candidates(user_id, &mutes, {});
    recommendations::score(candidates, clip_repo_.recommendation_profile(user_id));
    auto ranked = recommendations::select(std::move(candidates), 100);
    std::vector<std::string> ids;
    for (const auto &c : ranked)
        if (!c.clip.broadcaster_id.empty() &&
            std::find(ids.begin(), ids.end(), c.clip.broadcaster_id) == ids.end())
            ids.push_back(c.clip.broadcaster_id);
    auto cards = clip_repo_.get_broadcaster_cards(ids, limit);
    return {{"channels", cards}, {"count", cards.size()}};
}

void ClipService::record_clip_engagement(uint64_t clip_id, uint64_t user_id, float watched_sec,
                                         float duration_sec, bool session_final,
                                         const std::string &session_id) {
    if (user_id == 0)
        return;
    if (session_id.empty() || session_id.size() > 64 || !std::isfinite(watched_sec) ||
        !std::isfinite(duration_sec) || watched_sec < 0 || duration_sec <= 0)
        throw std::invalid_argument("Invalid playback event");
    auto clip = clip_repo_.find_by_id(clip_id);
    if (!clip)
        throw std::invalid_argument("Clip not found");
    duration_sec = clip->duration;
    if (duration_sec <= 0)
        throw std::invalid_argument("Unknown clip duration");
    watched_sec = std::min(watched_sec, duration_sec);
    clip_repo_.upsert_clip_engagement(user_id, clip_id, watched_sec, duration_sec, session_final);
    clip_repo_.record_analytics_engagement(user_id, session_id, clip_id, session_final, watched_sec);
}

bool ClipService::record_clip_feedback(uint64_t clip_id, uint64_t user_id, const std::string &action) {
    if (user_id == 0)
        return false;
    auto clip = clip_repo_.find_by_id(clip_id);
    if (!clip)
        return false;

    std::string key;
    if (action == "not_interested")
        key = "clip:" + std::to_string(clip_id);
    else if (action == "mute_game") {
        if (clip->game_id == 0)
            return false;
        key = "game:" + std::to_string(clip->game_id);
    } else if (action == "mute_channel") {
        if (!clip->broadcaster_id.empty())
            key = "bc:" + clip->broadcaster_id;
        else
            key = "bl:" + clip->broadcaster_name;
    } else
        return false;

    if (!clip_repo_.add_feed_block(user_id, key))
        return false;
    cache_.invalidate_pattern("clips:*:u:" + std::to_string(user_id));
    cache_.invalidate_pattern("clips:*:u:" + std::to_string(user_id) + ":cl:*");
    return true;
}

void ClipService::record_view(uint64_t clip_id, uint64_t user_id) {
    if (user_id)
        clip_repo_.record_user_view(user_id, clip_id);
}

bool ClipService::toggle_like(uint64_t clip_id, uint64_t user_id, bool is_like) {
    auto res = clip_repo_.upsert_user_like(user_id, clip_id, is_like);
    if (!res.ok)
        return false;
    cache_.del("clip:" + std::to_string(clip_id));
    return true;
}

bool ClipService::report_clip(uint64_t reporter_id, uint64_t clip_id, const std::string &reason) {
    if (!clip_repo_.find_by_id(clip_id))
        return false;
    return clip_repo_.insert_content_report(reporter_id, "clip", clip_id, reason);
}

std::vector<models::Clip>
ClipService::get_entity_clips(uint64_t user, const std::string &twitch, const std::string &scope,
                              uint32_t limit, uint32_t offset, uint64_t viewer,
                              const std::optional<std::vector<std::string>> &langs) {
    repositories::FeedMuteSet mutes;
    auto mp = resolve_mutes(clip_repo_, viewer, langs, mutes);
    return clip_repo_.find_entity_clips(user, twitch, scope, limit, offset, mp);
}
std::vector<models::Clip>
ClipService::get_following_feed(uint32_t limit, uint32_t offset, uint64_t user_id,
                                const std::optional<std::vector<std::string>> &langs) {
    repositories::FeedMuteSet mutes;
    auto mp = resolve_mutes(clip_repo_, user_id, langs, mutes);
    return clip_repo_.find_following_feed(user_id, limit, offset, mp);
}
std::vector<models::Clip>
ClipService::get_friends_feed(uint32_t limit, uint32_t offset, uint64_t user_id,
                              const std::optional<std::vector<std::string>> &clip_langs_q) {
    repositories::FeedMuteSet mutes;
    clip_repo_.load_feed_mutes(user_id, mutes);
    if (clip_langs_q.has_value())
        mutes.only_broadcast_langs = *clip_langs_q;
    return clip_repo_.find_friends_feed(user_id, limit, offset, &mutes);
}

std::vector<nlohmann::json> ClipService::get_games(uint64_t viewer_id) {
    return viewer_id ? clip_repo_.get_viewed_games(viewer_id) : clip_repo_.get_games();
}

uint64_t ClipService::count_clips_for_broadcaster(const std::string &twitch_login,
                                                  const std::string &twitch_user_id) {
    return clip_repo_.count_clips_for_broadcaster(twitch_login, twitch_user_id);
}

std::vector<models::Clip>
ClipService::get_clips_by_author(uint64_t author_id, uint32_t limit, uint32_t offset, uint64_t viewer_user_id,
                                 const std::optional<std::vector<std::string>> &clip_langs_q) {
    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mp = resolve_mutes(clip_repo_, viewer_user_id, clip_langs_q, mutes);
    return clip_repo_.find_by_author(author_id, limit, offset, mp);
}

std::vector<models::Clip>
ClipService::get_profile_clips(uint64_t user_id, const std::string &user_twitch_id, uint32_t limit,
                               uint32_t offset, uint64_t viewer_user_id,
                               const std::optional<std::vector<std::string>> &clip_langs_q) {
    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mp = resolve_mutes(clip_repo_, viewer_user_id, clip_langs_q, mutes);
    return clip_repo_.find_profile_clips(user_id, user_twitch_id, limit, offset, mp);
}

std::vector<nlohmann::json> ClipService::get_user_watch_activity(uint64_t user_id, uint32_t limit,
                                                                 uint32_t offset) {
    return clip_repo_.get_user_recent_view_activity(user_id, limit, offset);
}

std::vector<models::Clip>
ClipService::get_related_clips(uint64_t seed_clip_id, uint64_t viewer_user_id, uint32_t limit,
                               const std::optional<std::vector<std::string>> &clip_langs_q, uint32_t mix_seed,
                               const std::vector<uint64_t> &extra_exclude_clip_ids) {
    if (limit < 1)
        limit = 1;
    if (limit > 40)
        limit = 40;
    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mp = resolve_mutes(clip_repo_, viewer_user_id, clip_langs_q, mutes);
    const std::vector<uint64_t> *ex = extra_exclude_clip_ids.empty() ? nullptr : &extra_exclude_clip_ids;
    return clip_repo_.find_related_clips(seed_clip_id, viewer_user_id, limit, mp, mix_seed, ex);
}

void ClipService::invalidate_feed_cache_for_user(uint64_t user_id) {
    if (user_id == 0)
        return;
    cache_.invalidate_pattern("clips:*:u:" + std::to_string(user_id));
    cache_.invalidate_pattern("clips:*:u:" + std::to_string(user_id) + ":cl:*");
}

} // namespace clipstream::services
