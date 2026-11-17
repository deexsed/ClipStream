#pragma once
#include "models/Clip.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace clipstream::recommendations {
inline constexpr unsigned version = 2;
enum class Surface { Home, Rush };

struct Interaction {
    models::Clip clip;
    double weight{};
    double age_days{};
};

struct Profile {
    std::vector<Interaction> interactions;
    std::vector<std::string> followed_channels;
};

struct Candidate {
    models::Clip clip;
    double age_hours{};
    double likes{}, dislikes{}, opens{}, recent_opens{}, commenters{}, dismissals{};
    double twitch_velocity{};
    double collaborative{}; // signed, shrunk toward zero; unknown = zero
    double recently_opened{};
    bool evergreen{}, discovery{};
    bool seen_before{}, novel{}, followed{};
    double score{};
};

} // namespace clipstream::recommendations
