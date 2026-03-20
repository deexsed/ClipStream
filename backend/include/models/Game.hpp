#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace clipstream::models {

struct Game {
    uint64_t    id{};
    std::string twitch_id;
    std::string name;
    std::string box_art;
    std::string created_at;
};

inline void to_json(nlohmann::json& j, const Game& g) {
    j = {
        {"id",       g.id},
        {"twitch_id", g.twitch_id},
        {"name",     g.name},
        {"box_art",  g.box_art}
    };
}

} // namespace clipstream::models
