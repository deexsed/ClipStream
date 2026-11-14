#pragma once

#include "database/ConnectionPool.hpp"
#include "models/Clip.hpp"
#include "repositories/IRepository.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace clipstream::repositories {

enum class ClipSort { Popular, Recent, Duration };
enum class TimePeriod { Today, Week, Month, All };

/// Результат upsert лайка: дельта репутации владельца клипа (не учитывает self-vote — обнуляет сервис).
struct LikeUpsertResult {
    bool ok{false};
    int author_reputation_delta{0};
};

/// Минимальные поля для обновления метаданных при синке дубликатов.
struct ExistingClipRef {
    uint64_t id{};
    std::string broadcaster_id;
    std::string broadcaster_avatar;
};

struct ClipFilter {
    uint64_t game_id{};
    ClipSort sort{ClipSort::Popular};
    TimePeriod period{TimePeriod::Today};
    std::string search;
    std::string tag;
    std::string broadcaster_name;
};

/// Блокировки ленты (негативная персонализация)
struct FeedMuteSet {
    std::vector<uint64_t> exclude_clip_ids;
    std::vector<uint64_t> mute_game_ids;
    std::vector<std::string> mute_broadcaster_ids;
    std::vector<std::string> mute_broadcaster_logins;
    /// Из user_settings: скрывать клипы с флагом mature
    /// Подстроки в заголовке (NOT LIKE), из JSON user_settings.mute_keywords
    std::vector<std::string> mute_title_keywords;
    /// Пусто — все языки; иначе только эти коды (как у Twitch) + пустой язык у старых клипов
    std::vector<std::string> only_broadcast_langs;
};

struct RecommendationWeights {
    double hot_pref_game_boost{30.0};
    double hot_pref_broadcaster_boost{18.0};
    double rec_view_weight{0.35};
    double rec_like_weight{1.8};
    double rec_dislike_weight{1.2};
    double rec_freshness_weight{72.0};
    double rec_pref_game_boost{28.0};
    double rec_pref_broadcaster_boost{22.0};
    double rec_similar_broadcaster_boost{26.0};
    double rec_random_weight{12.0};
    /// Веса витрины признаков (feature_store_clip) в ранжировании «recommended»
    double rec_fs_velocity_weight{0.08};
    double rec_fs_er_weight{4.0};
    double rec_fs_fresh_decay_weight{6.0};
    /// Буст клипов из игр, которые пользователь смотрел за последние ~14 дней
    double rec_affinity_recent_game_boost{22.0};
    /// Игры, где пользователь реально смотрел клипы (user_clip_engagement), не только открыл карточку
    double rec_engagement_game_boost{16.0};
    /// Просмотры на платформе (view_history), LOG(1+n) в формуле ранжирования
    double rec_platform_view_weight{0.42};
    /// Комментарии на платформе, LOG(1+n)
    double rec_platform_comment_weight{1.75};
};

class ClipRepository : public IRepository<models::Clip> {
  public:
    explicit ClipRepository(database::ConnectionPool &pool);

    std::optional<models::Clip> find_by_id(uint64_t id) override;
    std::vector<models::Clip> find_all(uint32_t limit, uint32_t offset) override;
    uint64_t create(const models::Clip &clip) override;
    bool update(const models::Clip &clip) override;
    bool remove(uint64_t id) override;

    std::vector<models::Clip> find_filtered(const ClipFilter &filter, uint32_t limit, uint32_t offset,
                                            const FeedMuteSet *mutes = nullptr);
    /// Позиция в топе «популярное» с теми же фильтрами ленты, что и find_filtered (муты, язык из mutes).
    std::optional<uint32_t> popular_feed_position(uint64_t clip_id, TimePeriod period,
                                                  const FeedMuteSet *mutes, uint32_t cap);
    std::vector<models::Clip> find_by_author(uint64_t author_id, uint32_t limit, uint32_t offset,
                                             const FeedMuteSet *mutes = nullptr);
    /// Клипы пользователя: author_id на сайте ИЛИ совпадение Twitch creator_id с привязкой профиля.
    std::vector<models::Clip> find_profile_clips(uint64_t user_id, const std::string &user_twitch_id,
                                                 uint32_t limit, uint32_t offset,
                                                 const FeedMuteSet *mutes = nullptr);
    std::vector<models::Clip> find_personalized(const ClipFilter &filter,
                                                const std::vector<std::string> &broadcaster_names,
                                                uint32_t limit, uint32_t offset,
                                                const FeedMuteSet *mutes = nullptr);
    /// Клипы, которые недавно смотрели пользователи, на которых подписан viewer (внутренняя соц-лента).
    std::vector<models::Clip> find_entity_clips(uint64_t user, const std::string &twitch,
                                                const std::string &scope, uint32_t limit, uint32_t offset,
                                                const FeedMuteSet *mutes);
    std::vector<models::Clip> find_following_feed(uint64_t viewer, uint32_t limit, uint32_t offset,
                                                  const FeedMuteSet *mutes);
    std::vector<models::Clip> find_friends_feed(uint64_t viewer_user_id, uint32_t limit, uint32_t offset,
                                                const FeedMuteSet *mutes = nullptr);
    std::vector<models::Clip> find_hot(uint32_t limit, const std::vector<uint64_t> &exclude_clip_ids,
                                       const std::vector<uint64_t> &preferred_games,
                                       const std::vector<std::string> &preferred_broadcasters,
                                       const RecommendationWeights &weights,
                                       const FeedMuteSet *mutes = nullptr, bool with_tags = true);
    std::vector<models::Clip> find_fresh_random(uint32_t limit, const std::vector<uint64_t> &exclude_clip_ids,
                                                const std::vector<std::string> &tracked_broadcaster_ids,
                                                const FeedMuteSet *mutes = nullptr, bool with_tags = true);
    std::vector<models::Clip> find_recommended(uint32_t limit, const std::vector<uint64_t> &exclude_clip_ids,
                                               const std::vector<uint64_t> &preferred_games,
                                               const std::vector<std::string> &preferred_broadcasters,
                                               const std::vector<std::string> &similar_broadcasters,
                                               const RecommendationWeights &weights,
                                               uint64_t affinity_user_id, const FeedMuteSet *mutes = nullptr,
                                               bool with_tags = true);
    /// Одним запросом проставить теги (избегает N+1 на главной).
    void fill_tags_bulk(std::vector<models::Clip *> &clips);
    /// Коллаборативные «похожие»: co-view + контекст + промо. mix_seed — RAND(seed) для разнообразия;
    /// extra_exclude — id с клиента.
    std::vector<models::Clip> find_related_clips(uint64_t seed_clip_id, uint64_t viewer_user_id,
                                                 uint32_t limit, const FeedMuteSet *mutes, uint32_t mix_seed,
                                                 const std::vector<uint64_t> *extra_exclude);
    std::optional<nlohmann::json> find_user_brief_by_twitch_id(const std::string &twitch_user_id);
    /// Если горячее/новое/рекомендации пусты (узкие окна по дате и т.д.) — общий пул публичных клипов без
    /// порога просмотров.
    std::vector<models::Clip> find_public_clips_for_home_fallback(uint32_t limit,
                                                                  const FeedMuteSet *mutes = nullptr);
    std::vector<models::Clip> search(const std::string &query, uint32_t limit, uint32_t offset,
                                     const FeedMuteSet *mutes = nullptr);
    /// Много комментариев за последние `hours` часов (для блока «обсуждают сейчас»).
    std::vector<models::Clip> find_discussed_clips(uint32_t hours, uint32_t limit,
                                                   const FeedMuteSet *mutes = nullptr, bool with_tags = true);
    /// Недосмотренные клипы по user_clip_engagement (с heartbeat авторизованного зрителя).
    std::vector<models::Clip> find_continue_watching(uint64_t user_id, uint32_t limit,
                                                     const FeedMuteSet *mutes = nullptr,
                                                     bool with_tags = true);
    /// Свежие клипы каналов, которые пользователь недавно открывал (view_history).
    std::vector<models::Clip> find_clips_from_visited_channels(uint64_t user_id, uint32_t visit_days,
                                                               uint32_t clip_days, uint32_t limit,
                                                               const FeedMuteSet *mutes = nullptr,
                                                               bool with_tags = true);
    /// Один случайный клип из публичной выдачи (для кнопки «удачи»).
    std::optional<models::Clip> find_random_clip(const FeedMuteSet *mutes = nullptr, bool with_tags = true);
    bool record_user_view(uint64_t user_id, uint64_t clip_id);
    /// Был ли просмотр этой пары user+clip за последние `hours` часов (для анти-накрутки view_count).
    bool upsert_clip_engagement(uint64_t user_id, uint64_t clip_id, float watched_sec, float duration_sec,
                                bool session_final);
    void record_analytics_engagement(uint64_t user_id, const std::string &session_id, uint64_t clip_id,
                                     bool session_final, float watched_sec_total);
    bool refresh_feature_store_clips();
    bool refresh_feature_store_users();
    void record_home_impressions(uint64_t user_id, const std::string &session_id,
                                 const std::vector<models::Clip> &hot, const std::vector<models::Clip> &fresh,
                                 const std::vector<models::Clip> &recommended);
    nlohmann::json get_analytics_summary_24h();
    bool load_feed_mutes(uint64_t user_id, FeedMuteSet &out);
    bool add_feed_block(uint64_t user_id, const std::string &target_key);
    /// Учёт лайка/дизлайка в likes и корректировка счётчиков клипа.
    /// Повтор того же голоса = снять голос (DELETE из likes).
    LikeUpsertResult upsert_user_like(uint64_t user_id, uint64_t clip_id, bool is_like);
    /// Текущий голос пользователя: nullopt — нет строки; true — лайк; false — дизлайк.
    std::optional<bool> get_user_vote(uint64_t user_id, uint64_t clip_id);

    bool insert_content_report(uint64_t reporter_id, const std::string &target_type, uint64_t target_id,
                               const std::string &reason);
    std::vector<nlohmann::json> list_content_reports(uint32_t limit, uint32_t offset);
    bool mark_clip_unavailable(uint64_t clip_id, const std::string &reason, uint64_t actor_user_id);
    void log_compliance(const std::string &action, uint64_t clip_id, uint64_t actor_user_id,
                        const nlohmann::json &details);
    std::vector<nlohmann::json> get_user_recent_view_activity(uint64_t user_id, uint32_t limit,
                                                              uint32_t offset);
    std::vector<uint64_t> get_recent_viewed_clip_ids(uint64_t user_id, uint32_t limit);
    /// Какие из перечисленных клипов пользователь уже открывал (view_history), батчами по IN (...).
    std::unordered_set<uint64_t> clip_ids_seen_in_history(uint64_t user_id,
                                                          const std::vector<uint64_t> &clip_ids);
    std::vector<uint64_t> get_recent_active_user_ids(uint32_t days_back, uint32_t limit);
    std::vector<uint64_t> get_top_game_ids_by_user(uint64_t user_id, uint32_t limit);
    std::vector<std::string> get_top_broadcasters_by_user(uint64_t user_id, uint32_t limit);
    std::vector<std::string> get_similar_broadcasters(uint64_t user_id, uint32_t limit);
    /// Стримеры из просмотров людей, на которых зритель подписан на сайте (не Twitch).
    std::vector<std::string> get_network_broadcasters_from_site_follows(uint64_t viewer_id, uint32_t limit);
    /// «Смотрят того же стримера, что и вы» → какие ещё каналы смотрит эта аудитория (co-view).
    std::vector<std::string> get_coview_broadcasters_for_taste(uint64_t user_id, uint32_t limit);
    /// Случайные клипы для разнообразия главной (окно по дате + промо explore/feed/all).
    std::vector<models::Clip> find_home_explore(uint32_t limit, const std::vector<uint64_t> &exclude_clip_ids,
                                                const FeedMuteSet *mutes = nullptr, bool with_tags = true);
    bool upsert_user_recommendations(uint64_t user_id, const std::vector<std::string> &broadcaster_ids);
    std::vector<std::string> get_user_recommendations(uint64_t user_id, uint32_t max_age_hours,
                                                      uint32_t limit);
    std::vector<nlohmann::json> get_broadcaster_cards(const std::vector<std::string> &broadcaster_ids,
                                                      uint32_t limit);
    std::optional<std::string> get_app_setting(const std::string &setting_key);
    bool set_app_setting(const std::string &setting_key, const std::string &setting_value);

    std::vector<nlohmann::json> list_clip_promotions(uint32_t limit, uint32_t offset);
    std::optional<uint64_t> create_clip_promotion(uint64_t clip_id, double boost_score,
                                                  const std::string &placement, const std::string &label,
                                                  const std::string &starts_at_iso,
                                                  const std::string &ends_at_iso);
    bool delete_clip_promotion(uint64_t promo_id);

    bool ping_db();
    std::optional<models::Clip> find_by_twitch_id(const std::string &twitch_clip_id);

    /// Одним запросом для пакета ID (синк с Twitch без N+1).
    std::unordered_map<std::string, ExistingClipRef>
    find_existing_twitch_clip_refs(const std::vector<std::string> &twitch_clip_ids);

    uint64_t count_clips_for_broadcaster(const std::string &twitch_login, const std::string &twitch_user_id);
    std::vector<nlohmann::json> get_games();
    bool update_broadcaster_avatar(uint64_t id, const std::string &avatar_url);
    bool update_broadcaster_metadata(uint64_t id, const std::string &broadcaster_id,
                                     const std::string &avatar_url);

  private:
    friend class SyncRepository;
    static uint64_t create_on_connection(MYSQL *conn, const models::Clip &clip, uint64_t &affected);
    models::Clip row_to_clip(MYSQL_ROW row, MYSQL_RES *res);
    std::vector<std::string> load_tags(uint64_t clip_id);
    database::ConnectionPool &pool_;
};

} // namespace clipstream::repositories
