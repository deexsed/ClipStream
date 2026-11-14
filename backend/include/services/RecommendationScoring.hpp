#pragma once

#include "models/Recommendation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace clipstream::services::recommendations {

using ::clipstream::recommendations::Candidate;
using ::clipstream::recommendations::Interaction;
using ::clipstream::recommendations::Profile;
using ::clipstream::recommendations::Surface;
using ::clipstream::recommendations::version;

// All public scoring functions are deterministic and independent of SQL/Redis.
double approval(double likes, double dislikes);
double audience_quality(const Candidate &candidate);
double personal_match(const Profile &profile, const models::Clip &clip);
double title_match(const Profile &profile, const models::Clip &clip);
void score(std::vector<Candidate> &candidates, const Profile &profile, Surface surface = Surface::Home);
std::vector<Candidate> select(std::vector<Candidate> candidates, size_t limit, uint64_t seed = 0,
                              size_t offset = 0, Surface surface = Surface::Home);

} // namespace clipstream::services::recommendations
