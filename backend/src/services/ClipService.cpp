#include "services/ClipService.hpp"
#include "utils/Crypto.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_set>

namespace clipstream::services {

using json = nlohmann::json;

namespace {

std::string clip_day_bucket(const models::Clip &c) {
    const std::string &iso = !c.twitch_created_at.empty() ? c.twitch_created_at : c.created_at;
    if (iso.size() >= 10)
        return iso.substr(0, 10);
    return "?";
}

std::string fp_key(const models::Clip &c) {
    if (!c.title_fingerprint.empty())
        return c.title_fingerprint;
    return "id:" + std::to_string(c.id);
}

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
    storage.hide_mature = false;
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

std::vector<models::Clip> ClipService::diversify_feed(std::vector<models::Clip> ranked, uint32_t out_limit) {
    constexpr size_t kWindow = 12;
    constexpr size_t kMaxSameGame = 3;
    constexpr size_t kMaxSameFp = 1;
    constexpr size_t kMaxSameDay = 4;

    std::vector<models::Clip> result;
    result.reserve(std::min(static_cast<size_t>(out_limit), ranked.size()));

    std::deque<std::tuple<uint64_t, std::string, std::string>> win;

    auto count_in_window = [&](uint64_t game, const std::string &fp, const std::string &day) {
        size_t cg = 0, cf = 0, cd = 0;
        for (const auto &t : win) {
            if (std::get<0>(t) == game)
                ++cg;
            if (std::get<1>(t) == fp)
                ++cf;
            if (std::get<2>(t) == day)
                ++cd;
        }
        return std::tuple{cg, cf, cd};
    };

    auto push_win = [&](const models::Clip &c) {
        win.push_back({c.game_id, fp_key(c), clip_day_bucket(c)});
        if (win.size() > kWindow)
            win.pop_front();
    };

    auto ok_constraints = [&](const models::Clip &c, bool relax_fp) {
        auto [cg, cf, cd] = count_in_window(c.game_id, fp_key(c), clip_day_bucket(c));
        if (cg >= kMaxSameGame || cd >= kMaxSameDay)
            return false;
        if (!relax_fp && cf >= kMaxSameFp)
            return false;
        return true;
    };

    auto already = [&](uint64_t id) {
        for (const auto &x : result)
            if (x.id == id)
                return true;
        return false;
    };

    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            win.clear();
            for (const auto &c : result)
                push_win(c);
        }
        const bool relax = (pass == 1);
        for (const auto &c : ranked) {
            if (result.size() >= out_limit)
                break;
            if (already(c.id))
                continue;
            if (!ok_constraints(c, relax))
                continue;
            result.push_back(c);
            push_win(c);
        }
        if (result.size() >= out_limit)
            break;
    }

    for (const auto &c : ranked) {
        if (result.size() >= out_limit)
            break;
        if (!already(c.id))
            result.push_back(c);
    }

    return result;
}

static bool take_non_dup(std::vector<models::Clip> &pool, size_t &i, std::unordered_set<uint64_t> &seen,
                         models::Clip &out) {
    while (i < pool.size()) {
        if (!seen.count(pool[i].id)) {
            seen.insert(pool[i].id);
            out = std::move(pool[i]);
            ++i;
            return true;
        }
        ++i;
    }
    return false;
}

static void merge_broadcaster_ids(std::vector<std::string> &dest, const std::vector<std::string> &add,
                                  size_t max_len) {
    std::unordered_set<std::string> seen;
    for (const auto &s : dest)
        if (!s.empty())
            seen.insert(s);
    for (const auto &s : add) {
        if (dest.size() >= max_len)
            break;
        if (s.empty() || seen.count(s))
            continue;
        seen.insert(s);
        dest.push_back(s);
    }
}

static void mix_home_streams(std::vector<models::Clip> &hot, std::vector<models::Clip> &fresh,
                             std::vector<models::Clip> &rec, std::vector<models::Clip> &explore,
                             uint32_t limit, std::vector<std::pair<std::string, models::Clip>> &out) {
    out.clear();
    std::unordered_set<uint64_t> seen;
    size_t ih = 0, ifr = 0, ir = 0, iex = 0;
    uint32_t turn = 0;
    models::Clip tmp;
    while (out.size() < limit) {
        if (!explore.empty() && (out.size() % 11u == 5u)) {
            if (take_non_dup(explore, iex, seen, tmp)) {
                out.emplace_back("explore", std::move(tmp));
                continue;
            }
        }
        const char *shelf = "for_you";
        bool ok = false;
        if (turn % 3 == 0) {
            shelf = "for_you";
            ok = take_non_dup(rec, ir, seen, tmp);
        } else if (turn % 3 == 1) {
            shelf = "trending";
            ok = take_non_dup(hot, ih, seen, tmp);
        } else {
            shelf = "fresh";
            ok = take_non_dup(fresh, ifr, seen, tmp);
        }
        ++turn;
        if (ok) {
            out.emplace_back(shelf, std::move(tmp));
            continue;
        }
        if (take_non_dup(rec, ir, seen, tmp)) {
            out.emplace_back("for_you", std::move(tmp));
            continue;
        }
        if (take_non_dup(hot, ih, seen, tmp)) {
            out.emplace_back("trending", std::move(tmp));
            continue;
        }
        if (take_non_dup(fresh, ifr, seen, tmp)) {
            out.emplace_back("fresh", std::move(tmp));
            continue;
        }
        if (take_non_dup(explore, iex, seen, tmp)) {
            out.emplace_back("explore", std::move(tmp));
            continue;
        }
        break;
    }
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
        return clips;
    }

    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mp = resolve_mutes(clip_repo_, user_id, clip_langs_q, mutes);

    auto clips = clip_repo_.find_filtered(filter, limit, offset, mp);
    json j = json::array();
    for (auto &c : clips)
        j.push_back(c);
    cache_.set(key, j.dump(), 60);
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

static std::vector<uint64_t> viewed_ids_from_cache(const std::string &raw) {
    std::vector<uint64_t> ids;
    try {
        auto j = json::parse(raw);
        if (!j.is_array())
            return ids;
        ids.reserve(j.size());
        for (const auto &item : j) {
            if (item.is_number_unsigned())
                ids.push_back(item.get<uint64_t>());
        }
    } catch (...) {
    }
    return ids;
}

static std::string viewed_ids_to_cache(const std::vector<uint64_t> &ids) {
    json j = json::array();
    for (auto id : ids)
        j.push_back(id);
    return j.dump();
}

nlohmann::json ClipService::get_home_sections(uint64_t user_id, uint32_t per_section,
                                              const std::vector<std::string> &tracked_broadcaster_ids,
                                              const std::string &session_id,
                                              const std::optional<std::vector<std::string>> &clip_langs_q) {
    std::vector<uint64_t> viewed_ids;
    std::vector<uint64_t> preferred_games;
    std::vector<std::string> preferred_broadcasters;
    std::vector<std::string> similar_broadcasters;
    std::vector<std::string> tracked_effective = tracked_broadcaster_ids;

    if (user_id != 0) {
        const auto views_key = "user:views:" + std::to_string(user_id);
        auto cached = cache_.get(views_key);
        if (cached)
            viewed_ids = viewed_ids_from_cache(*cached);
        if (viewed_ids.empty()) {
            viewed_ids = clip_repo_.get_recent_viewed_clip_ids(user_id, 180);
            if (!viewed_ids.empty())
                cache_.set(views_key, viewed_ids_to_cache(viewed_ids), 60 * 60 * 24 * 30);
        }
        preferred_games = clip_repo_.get_top_game_ids_by_user(user_id, 7);
        preferred_broadcasters = clip_repo_.get_top_broadcasters_by_user(user_id, 11);
        const auto similar_key = "user:similar:" + std::to_string(user_id);
        auto cached_similar = cache_.get(similar_key);
        if (cached_similar) {
            try {
                auto j = json::parse(*cached_similar);
                if (j.is_array()) {
                    for (const auto &item : j) {
                        if (item.is_string())
                            similar_broadcasters.push_back(item.get<std::string>());
                    }
                }
            } catch (...) {
            }
        }
        if (similar_broadcasters.empty()) {
            similar_broadcasters = clip_repo_.get_user_recommendations(user_id, 24, 14);
            if (similar_broadcasters.empty()) {
                similar_broadcasters = clip_repo_.get_similar_broadcasters(user_id, 14);
                if (!similar_broadcasters.empty()) {
                    clip_repo_.upsert_user_recommendations(user_id, similar_broadcasters);
                }
            }
            json j = json::array();
            for (const auto &id : similar_broadcasters)
                j.push_back(id);
            cache_.set(similar_key, j.dump(), 60 * 60 * 24);
        }
        const auto site_net = clip_repo_.get_network_broadcasters_from_site_follows(user_id, 18);
        merge_broadcaster_ids(preferred_broadcasters, site_net, 16);
        merge_broadcaster_ids(similar_broadcasters, site_net, 32);
        merge_broadcaster_ids(similar_broadcasters, clip_repo_.get_coview_broadcasters_for_taste(user_id, 18),
                              32);
        merge_broadcaster_ids(tracked_effective, site_net, 16);
    }

    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mute_ptr = resolve_mutes(clip_repo_, user_id, clip_langs_q, mutes);

    constexpr uint32_t kCandMul = 4;
    auto hot = clip_repo_.find_hot(per_section * kCandMul, viewed_ids, preferred_games,
                                   preferred_broadcasters, rec_weights_, mute_ptr, false);
    hot = diversify_feed(std::move(hot), per_section);
    auto fresh =
        clip_repo_.find_fresh_random(per_section * kCandMul, viewed_ids, tracked_effective, mute_ptr, false);
    fresh = diversify_feed(std::move(fresh), per_section);

    std::vector<uint64_t> exclude_for_recommended = viewed_ids;
    exclude_for_recommended.reserve(viewed_ids.size() + hot.size() + fresh.size());
    for (const auto &c : hot)
        exclude_for_recommended.push_back(c.id);
    for (const auto &c : fresh)
        exclude_for_recommended.push_back(c.id);
    std::sort(exclude_for_recommended.begin(), exclude_for_recommended.end());
    exclude_for_recommended.erase(std::unique(exclude_for_recommended.begin(), exclude_for_recommended.end()),
                                  exclude_for_recommended.end());

    auto recommended = clip_repo_.find_recommended(
        per_section * kCandMul, exclude_for_recommended, preferred_games, preferred_broadcasters,
        similar_broadcasters, rec_weights_, user_id, mute_ptr, false);
    recommended = diversify_feed(std::move(recommended), per_section);
    if (recommended.empty()) {
        recommended = clip_repo_.find_hot(per_section * kCandMul, exclude_for_recommended, {}, {},
                                          rec_weights_, mute_ptr, false);
        recommended = diversify_feed(std::move(recommended), per_section);
    }

    if (hot.empty() && fresh.empty() && recommended.empty()) {
        auto pool = clip_repo_.find_public_clips_for_home_fallback(per_section * 6, mute_ptr);
        if (!pool.empty()) {
            hot = diversify_feed(std::vector<models::Clip>(pool), per_section);
            fresh = diversify_feed(std::vector<models::Clip>(pool), per_section);
            recommended = diversify_feed(std::vector<models::Clip>(pool), per_section);
        }
    }

    {
        std::vector<models::Clip *> tag_ptrs;
        for (auto &c : hot)
            tag_ptrs.push_back(&c);
        for (auto &c : fresh)
            tag_ptrs.push_back(&c);
        for (auto &c : recommended)
            tag_ptrs.push_back(&c);
        clip_repo_.fill_tags_bulk(tag_ptrs);
    }

    clip_repo_.record_home_impressions(user_id, session_id, hot, fresh, recommended);

    json out;
    out["hot"] = json::array();
    out["fresh"] = json::array();
    out["recommended"] = json::array();
    for (const auto &c : hot)
        out["hot"].push_back(c);
    for (const auto &c : fresh)
        out["fresh"].push_back(c);
    for (const auto &c : recommended)
        out["recommended"].push_back(c);
    return out;
}

nlohmann::json ClipService::get_home_feed(uint64_t user_id, uint32_t limit,
                                          const std::vector<uint64_t> &client_exclude_ids,
                                          const std::string &session_id,
                                          const std::optional<std::vector<std::string>> &clip_langs_q) {
    if (limit < 6)
        limit = 6;
    if (limit > 48)
        limit = 48;

    std::vector<uint64_t> viewed_ids;
    std::vector<uint64_t> preferred_games;
    std::vector<std::string> preferred_broadcasters;
    std::vector<std::string> similar_broadcasters;

    if (user_id != 0) {
        const auto views_key = "user:views:" + std::to_string(user_id);
        auto cached = cache_.get(views_key);
        if (cached)
            viewed_ids = viewed_ids_from_cache(*cached);
        if (viewed_ids.empty()) {
            viewed_ids = clip_repo_.get_recent_viewed_clip_ids(user_id, 180);
            if (!viewed_ids.empty())
                cache_.set(views_key, viewed_ids_to_cache(viewed_ids), 60 * 60 * 24 * 30);
        }
        preferred_games = clip_repo_.get_top_game_ids_by_user(user_id, 7);
        preferred_broadcasters = clip_repo_.get_top_broadcasters_by_user(user_id, 11);
        const auto similar_key = "user:similar:" + std::to_string(user_id);
        auto cached_similar = cache_.get(similar_key);
        if (cached_similar) {
            try {
                auto j = json::parse(*cached_similar);
                if (j.is_array()) {
                    for (const auto &item : j) {
                        if (item.is_string())
                            similar_broadcasters.push_back(item.get<std::string>());
                    }
                }
            } catch (...) {
            }
        }
        if (similar_broadcasters.empty()) {
            similar_broadcasters = clip_repo_.get_user_recommendations(user_id, 24, 14);
            if (similar_broadcasters.empty()) {
                similar_broadcasters = clip_repo_.get_similar_broadcasters(user_id, 14);
                if (!similar_broadcasters.empty())
                    clip_repo_.upsert_user_recommendations(user_id, similar_broadcasters);
            }
            json j = json::array();
            for (const auto &id : similar_broadcasters)
                j.push_back(id);
            cache_.set(similar_key, j.dump(), 60 * 60 * 24);
        }
        const auto site_net = clip_repo_.get_network_broadcasters_from_site_follows(user_id, 18);
        merge_broadcaster_ids(preferred_broadcasters, site_net, 16);
        merge_broadcaster_ids(similar_broadcasters, site_net, 32);
        merge_broadcaster_ids(similar_broadcasters, clip_repo_.get_coview_broadcasters_for_taste(user_id, 18),
                              32);
    }

    std::vector<uint64_t> exclude = viewed_ids;
    exclude.insert(exclude.end(), client_exclude_ids.begin(), client_exclude_ids.end());
    std::sort(exclude.begin(), exclude.end());
    exclude.erase(std::unique(exclude.begin(), exclude.end()), exclude.end());
    if (exclude.size() > 220)
        exclude.resize(220);

    repositories::FeedMuteSet mutes;
    const repositories::FeedMuteSet *mute_ptr = resolve_mutes(clip_repo_, user_id, clip_langs_q, mutes);

    constexpr uint32_t kMul = 6;
    const uint32_t pool_cap = std::max(limit * kMul, 72u);

    std::vector<std::string> no_tracked;
    auto hot = clip_repo_.find_hot(pool_cap, exclude, preferred_games, preferred_broadcasters, rec_weights_,
                                   mute_ptr, false);
    hot = diversify_feed(std::move(hot), std::max(limit * 3u, 24u));
    auto fresh = clip_repo_.find_fresh_random(pool_cap, exclude, no_tracked, mute_ptr, false);
    fresh = diversify_feed(std::move(fresh), std::max(limit * 3u, 24u));

    std::vector<uint64_t> exclude_rec = exclude;
    for (const auto &c : hot)
        exclude_rec.push_back(c.id);
    for (const auto &c : fresh)
        exclude_rec.push_back(c.id);
    std::sort(exclude_rec.begin(), exclude_rec.end());
    exclude_rec.erase(std::unique(exclude_rec.begin(), exclude_rec.end()), exclude_rec.end());
    if (exclude_rec.size() > 220)
        exclude_rec.resize(220);

    auto recommended =
        clip_repo_.find_recommended(pool_cap, exclude_rec, preferred_games, preferred_broadcasters,
                                    similar_broadcasters, rec_weights_, user_id, mute_ptr, false);
    recommended = diversify_feed(std::move(recommended), std::max(limit * 3u, 24u));
    if (recommended.empty()) {
        recommended = clip_repo_.find_hot(pool_cap, exclude_rec, {}, {}, rec_weights_, mute_ptr, false);
        recommended = diversify_feed(std::move(recommended), std::max(limit * 3u, 24u));
    }

    auto explore = clip_repo_.find_home_explore(std::max(32u, limit * 2u), exclude, mute_ptr, false);
    explore = diversify_feed(std::move(explore), std::max(18u, limit + 6u));

    std::vector<std::pair<std::string, models::Clip>> mixed;
    mix_home_streams(hot, fresh, recommended, explore, limit, mixed);

    if (mixed.empty()) {
        auto pool = clip_repo_.find_public_clips_for_home_fallback(std::max(limit * 2u, 24u), mute_ptr);
        pool = diversify_feed(std::move(pool), limit);
        mixed.reserve(pool.size());
        for (auto &c : pool)
            mixed.emplace_back("for_you", std::move(c));
    }

    std::vector<models::Clip *> tag_ptrs;
    tag_ptrs.reserve(mixed.size());
    for (auto &pr : mixed)
        tag_ptrs.push_back(&pr.second);
    clip_repo_.fill_tags_bulk(tag_ptrs);

    if (client_exclude_ids.empty() && !session_id.empty()) {
        std::vector<models::Clip> h, f, r;
        for (const auto &pr : mixed) {
            if (pr.first == "trending")
                h.push_back(pr.second);
            else if (pr.first == "fresh")
                f.push_back(pr.second);
            else
                r.push_back(pr.second);
        }
        clip_repo_.record_home_impressions(user_id, session_id, h, f, r);
    }

    std::vector<uint64_t> mixed_ids;
    mixed_ids.reserve(mixed.size());
    for (const auto &pr : mixed)
        mixed_ids.push_back(pr.second.id);
    const auto viewed_in_feed = user_id != 0 ? clip_repo_.clip_ids_seen_in_history(user_id, mixed_ids)
                                             : std::unordered_set<uint64_t>{};

    json out = json::array();
    for (const auto &pr : mixed) {
        json row = json::object();
        row["shelf"] = pr.first;
        json clipj;
        models::to_json(clipj, pr.second);
        if (user_id != 0 && viewed_in_feed.count(pr.second.id))
            clipj["viewed"] = true;
        row["clip"] = std::move(clipj);
        out.push_back(std::move(row));
    }
    return {{"items", std::move(out)}, {"count", mixed.size()}};
}

nlohmann::json ClipService::get_analytics_summary_24h() { return clip_repo_.get_analytics_summary_24h(); }

nlohmann::json ClipService::get_similar_streamers(uint64_t user_id, uint32_t limit) {
    if (user_id == 0)
        return {{"channels", json::array()}, {"count", 0}};

    std::vector<std::string> ids = clip_repo_.get_user_recommendations(user_id, 24, limit);
    if (ids.empty()) {
        ids = clip_repo_.get_similar_broadcasters(user_id, limit);
        if (!ids.empty())
            clip_repo_.upsert_user_recommendations(user_id, ids);
    }
    auto cards = clip_repo_.get_broadcaster_cards(ids, limit);
    return {{"channels", cards}, {"count", cards.size()}};
}

void ClipService::recompute_recommendations_for_active_users(uint32_t days_back, uint32_t max_users) {
    auto users = clip_repo_.get_recent_active_user_ids(days_back, max_users);
    for (auto user_id : users) {
        auto similar = clip_repo_.get_similar_broadcasters(user_id, 12);
        if (!similar.empty())
            clip_repo_.upsert_user_recommendations(user_id, similar);
    }
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
    if (user_id != 0) {

        clip_repo_.record_user_view(user_id, clip_id);
        const auto views_key = "user:views:" + std::to_string(user_id);
        auto existing = cache_.get(views_key);
        auto ids = existing ? viewed_ids_from_cache(*existing) : std::vector<uint64_t>{};
        ids.erase(std::remove(ids.begin(), ids.end(), clip_id), ids.end());
        ids.insert(ids.begin(), clip_id);
        if (ids.size() > 180)
            ids.resize(180);
        cache_.set(views_key, viewed_ids_to_cache(ids), 60 * 60 * 24 * 30);
    }
    cache_.del("clip:" + std::to_string(clip_id));
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

std::vector<nlohmann::json> ClipService::get_games() { return clip_repo_.get_games(); }

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
