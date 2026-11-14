#pragma once

#include "models/Clip.hpp"
#include "repositories/ClipRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "services/CacheService.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace clipstream::services {

class ClipService {
  public:
    ClipService(repositories::ClipRepository &clip_repo, repositories::UserRepository &user_repo,
                CacheService &cache);

    std::vector<models::Clip>
    get_feed(const repositories::ClipFilter &filter, uint32_t limit, uint32_t offset, uint64_t user_id = 0,
             const std::optional<std::vector<std::string>> &clip_langs_q = std::nullopt);
    std::vector<models::Clip>
    get_personalized_feed(const repositories::ClipFilter &filter,
                          const std::vector<std::string> &followed_broadcasters, uint32_t limit,
                          uint32_t offset, uint64_t user_id = 0,
                          const std::optional<std::vector<std::string>> &clip_langs_q = std::nullopt);
    std::vector<models::Clip>
    get_entity_clips(uint64_t user, const std::string &twitch, const std::string &scope, uint32_t limit,
                     uint32_t offset, uint64_t viewer,
                     const std::optional<std::vector<std::string>> &langs = std::nullopt);
    std::vector<models::Clip>
    get_following_feed(uint32_t limit, uint32_t offset, uint64_t user_id,
                       const std::optional<std::vector<std::string>> &clip_langs_q = std::nullopt);
    std::vector<models::Clip>
    get_friends_feed(uint32_t limit, uint32_t offset, uint64_t user_id,
                     const std::optional<std::vector<std::string>> &clip_langs_q = std::nullopt);
    nlohmann::json
    get_home_sections(uint64_t user_id, uint32_t per_section,
                      const std::vector<std::string> &tracked_broadcaster_ids = {},
                      const std::string &session_id = {},
                      const std::optional<std::vector<std::string>> &clip_langs_q = std::nullopt);
    /// Bounded, stable recommendation queue; cursor is opaque and scoped to viewer/languages/surface.
    nlohmann::json get_home_feed(uint64_t user_id, uint32_t limit,
                                 const std::string &cursor,
                                 const std::string &session_id = {},
                                 const std::optional<std::vector<std::string>> &clip_langs_q = std::nullopt,
                                 const std::string &surface = "home");
    void record_recommendation_impressions(uint64_t user_id, const std::vector<std::string> &tokens);
    void save_recommendation_twitch_follows(uint64_t user_id, const nlohmann::json &channels);
    nlohmann::json get_analytics_summary_24h();
    nlohmann::json get_similar_streamers(uint64_t user_id, uint32_t limit);
    std::optional<models::Clip> get_clip(uint64_t id);
    /// JSON карточки клипа + user_vote для зрителя (like / dislike / null). Без кэша — актуальные счётчики.
    std::optional<nlohmann::json> get_clip_json_for_viewer(uint64_t clip_id, uint64_t viewer_user_id);
    /// Чарты трендов: для каждого периода { world?, lang? } — места в топ-50 (как страница /trends).
    std::optional<nlohmann::json> get_clip_trend_ranks(uint64_t clip_id, uint64_t viewer_user_id);
    std::vector<models::Clip>
    search(const std::string &query, uint32_t limit, uint32_t offset, uint64_t viewer_user_id,
           const std::optional<std::vector<std::string>> &clip_langs_q = std::nullopt);
    std::vector<models::Clip>
    get_discussed_clips(uint32_t hours, uint32_t limit, uint64_t viewer_user_id,
                        const std::optional<std::vector<std::string>> &clip_langs_q);
    std::vector<models::Clip>
    get_continue_watching(uint32_t limit, uint64_t viewer_user_id,
                          const std::optional<std::vector<std::string>> &clip_langs_q);
    std::vector<models::Clip>
    get_channel_updates_for_viewer(uint32_t limit, uint64_t viewer_user_id,
                                   const std::optional<std::vector<std::string>> &clip_langs_q);
    std::optional<models::Clip> get_random_clip(uint64_t viewer_user_id,
                                                const std::optional<std::vector<std::string>> &clip_langs_q);
    void record_view(uint64_t clip_id, uint64_t user_id);
    void record_clip_engagement(uint64_t clip_id, uint64_t user_id, float watched_sec, float duration_sec,
                                bool session_final, const std::string &session_id = {});
    bool record_clip_feedback(uint64_t clip_id, uint64_t user_id, const std::string &action);
    bool toggle_like(uint64_t clip_id, uint64_t user_id, bool is_like);
    bool report_clip(uint64_t reporter_id, uint64_t clip_id, const std::string &reason);
    std::vector<nlohmann::json> get_games(uint64_t viewer_id = 0);
    uint64_t count_clips_for_broadcaster(const std::string &twitch_login, const std::string &twitch_user_id);
    std::vector<models::Clip>
    get_clips_by_author(uint64_t author_id, uint32_t limit, uint32_t offset, uint64_t viewer_user_id,
                        const std::optional<std::vector<std::string>> &clip_langs_q = std::nullopt);
    /// Клипы, созданные пользователем на Twitch (creator_id) или привязанные как author_id.
    std::vector<models::Clip>
    get_profile_clips(uint64_t user_id, const std::string &user_twitch_id, uint32_t limit, uint32_t offset,
                      uint64_t viewer_user_id,
                      const std::optional<std::vector<std::string>> &clip_langs_q = std::nullopt);
    std::vector<nlohmann::json> get_user_watch_activity(uint64_t user_id, uint32_t limit, uint32_t offset);
    void invalidate_feed_cache_for_user(uint64_t user_id);
    /// Похожие клипы (co-view + контекст + промо). mix_seed и exclude меняют порядок/состав между запросами.
    std::vector<models::Clip> get_related_clips(uint64_t seed_clip_id, uint64_t viewer_user_id,
                                                uint32_t limit,
                                                const std::optional<std::vector<std::string>> &clip_langs_q,
                                                uint32_t mix_seed,
                                                const std::vector<uint64_t> &extra_exclude_clip_ids);

  private:
    std::string cache_key(const repositories::ClipFilter &f, uint32_t limit, uint32_t offset,
                          uint64_t user_id);

    repositories::ClipRepository &clip_repo_;
    repositories::UserRepository &user_repo_;
    CacheService &cache_;
};

} // namespace clipstream::services
