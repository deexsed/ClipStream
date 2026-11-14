#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace clipstream::models {

struct Clip {
    uint64_t id{};
    std::string twitch_clip_id;
    std::string title;
    std::string title_fingerprint; ///< дедуп похожих заголовков в выдаче (не отдаётся в JSON)
    std::string thumbnail_url;
    std::string embed_url;
    float duration{};
    uint32_t view_count{};
    int like_count{};
    int dislike_count{};
    std::string broadcaster_name;
    std::string broadcaster_login;
    std::string broadcaster_id;
    std::string broadcaster_avatar;
    uint64_t game_id{};
    uint64_t author_id{};
    /// Кто создал клип на Twitch (Helix: creator_id / creator_name — логин).
    std::string creator_twitch_id;
    std::string creator_login;
    std::string created_at;
    std::string game_name;
    std::string vod_id;
    int vod_offset{};
    std::string twitch_created_at;
    /// Язык трансляции из Helix clips (ISO 639-1), например en, ru; пусто — неизвестно/старые записи
    std::string broadcaster_lang;
    bool is_mature{};
    std::string classification_source{"unknown"};

    /// Подпись витрины (продвижение), только если задана админом
    std::string promo_label;

    std::vector<std::string> tags;
};

inline void to_json(nlohmann::json &j, const Clip &c) {
    j = {{"id", c.id},
         {"twitch_clip_id", c.twitch_clip_id},
         {"title", c.title},
         {"thumbnail_url", c.thumbnail_url},
         {"embed_url", c.embed_url},
         {"duration", c.duration},
         {"view_count", c.view_count},
         {"like_count", c.like_count},
         {"dislike_count", c.dislike_count},
         {"broadcaster_name", c.broadcaster_name},
         {"broadcaster_login", c.broadcaster_login},
         {"broadcaster_id", c.broadcaster_id},
         {"broadcaster_avatar", c.broadcaster_avatar},
         {"game_id", c.game_id},
         {"game_name", c.game_name},
         {"tags", c.tags},
         {"created_at", c.created_at},
         {"vod_id", c.vod_id},
         {"vod_offset", c.vod_offset},
         {"twitch_created_at", c.twitch_created_at},
         {"language", c.broadcaster_lang},
         {"is_mature", c.is_mature},
         {"classification_source", c.classification_source}};
    if (!c.promo_label.empty())
        j["promo_label"] = c.promo_label;
    if (!c.creator_login.empty())
        j["creator_login"] = c.creator_login;
    if (!c.creator_twitch_id.empty())
        j["creator_twitch_id"] = c.creator_twitch_id;
    if (c.author_id)
        j["author_id"] = c.author_id;
}

} // namespace clipstream::models
