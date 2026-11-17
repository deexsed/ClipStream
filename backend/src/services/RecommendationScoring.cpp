#include "services/RecommendationScoring.hpp"
#include <cctype>
#include <deque>
#include <map>
#include <set>
#include <unordered_set>

namespace clipstream::services::recommendations {
namespace {
double bounded_log(double value, double scale) {
    return std::clamp(std::log1p(std::max(0.0, value)) / std::log1p(scale), 0.0, 1.0);
}
// UTF-8 words, including Cyrillic case folding, without process-global locale
// state.
std::set<std::string> words(const std::string &text) {
    std::set<std::string> result;
    std::string word;
    auto flush = [&] {
        if (word.size() >= 3)
            result.insert(word);
        word.clear();
    };
    for (size_t i = 0; i < text.size();) {
        auto a = static_cast<unsigned char>(text[i]);
        if (a < 128) {
            if (std::isalnum(a))
                word += static_cast<char>(std::tolower(a));
            else
                flush();
            ++i;
        } else if (i + 1 < text.size() && (a == 0xd0 || a == 0xd1)) {
            auto b = static_cast<unsigned char>(text[i + 1]);
            unsigned cp = ((a & 31u) << 6u) | (b & 63u);
            if (cp >= 0x410 && cp <= 0x42f)
                cp += 32;
            if (cp == 0x401)
                cp = 0x451;
            word += static_cast<char>(0xc0u | (cp >> 6u));
            word += static_cast<char>(0x80u | (cp & 63u));
            i += 2;
        } else {
            // Keep other UTF-8 sequences intact. They are never lowercased byte by
            // byte.
            const size_t n = a >= 0xf0 ? 4 : a >= 0xe0 ? 3 : 2;
            word.append(text, i, std::min(n, text.size() - i));
            i += std::min(n, text.size() - i);
        }
    }
    flush();
    return result;
}
double decay(double days) {
    days = std::max(0.0, days);
    return 0.65 * std::exp2(-days / 7) + 0.35 * std::exp2(-days / 60);
}
uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
bool same_moment(const models::Clip &a, const models::Clip &b) {
    if (a.id == b.id)
        return true;
    if (!a.vod_id.empty() && a.vod_id == b.vod_id) {
        const double overlap = std::min(a.vod_offset + a.duration, b.vod_offset + b.duration) -
                               std::max(a.vod_offset, b.vod_offset);
        return overlap > 0 && overlap >= 0.7 * std::min(a.duration, b.duration);
    }
    return !a.title_fingerprint.empty() && a.title_fingerprint == b.title_fingerprint;
}
} // namespace

double approval(double likes, double dislikes) {
    likes = std::max(0.0, likes);
    dislikes = std::max(0.0, dislikes);
    return (likes + 16.0) / (likes + dislikes + 20.0);
}
double audience_quality(const Candidate &c) {
    // Vote balance and absolute support are distinct. No historical votes /
    // recent opens ratio.
    const double rejection = c.dismissals / (c.opens + c.dismissals + 20.0);
    return std::clamp(0.50 * approval(c.likes, c.dislikes) + 0.15 * bounded_log(c.likes, 500) +
                          0.15 * bounded_log(c.opens, 2000) + 0.10 * bounded_log(c.recent_opens, 300) +
                          0.10 * bounded_log(c.commenters, 100) - 0.30 * rejection,
                      0.0, 1.0);
}
double personal_match(const Profile &p, const models::Clip &c) {
    double sum = 0, support = 0;
    for (const auto &i : p.interactions) {
        double match = 0;
        if (!c.broadcaster_id.empty() && c.broadcaster_id == i.clip.broadcaster_id)
            match += 1;
        if (c.game_id && c.game_id == i.clip.game_id)
            match += i.weight < 0 ? 0.15 : 0.5;
        if (!c.creator_twitch_id.empty() && c.creator_twitch_id == i.clip.creator_twitch_id)
            match += 0.3;
        const double w = i.weight * decay(i.age_days);
        sum += w * match;
        support += std::abs(w) * match;
    }
    if (std::find(p.followed_channels.begin(), p.followed_channels.end(), c.broadcaster_id) !=
        p.followed_channels.end()) {
        sum += 4;
        support += 4;
    }
    return std::clamp(0.5 + 0.5 * sum / (support + 4), 0.0, 1.0);
}
namespace {
struct TextProfile {
    std::unordered_map<std::string, size_t> frequencies;
    std::unordered_map<std::string, double> signed_terms, absolute_terms;
    size_t count{};
    double idf(const std::string &term) const {
        auto it = frequencies.find(term);
        return 1 + std::log((count + 1.0) / ((it == frequencies.end() ? 0 : it->second) + 1.0));
    }
    explicit TextProfile(const Profile &p) : count(p.interactions.size()) {
        std::vector<std::set<std::string>> docs;
        for (const auto &i : p.interactions) {
            docs.push_back(words(i.clip.title));
            for (const auto &t : docs.back())
                ++frequencies[t];
        }
        for (size_t i = 0; i < docs.size(); ++i) {
            double norm = 0;
            for (const auto &t : docs[i]) {
                const auto v = idf(t);
                norm += v * v;
            }
            if (norm == 0)
                continue;
            const double w = p.interactions[i].weight * decay(p.interactions[i].age_days) / std::sqrt(norm);
            for (const auto &t : docs[i]) {
                const auto v = idf(t);
                signed_terms[t] += w * v * v;
                absolute_terms[t] += std::abs(w) * v * v;
            }
        }
    }
    double match(const std::string &title) const {
        const auto tokens = words(title);
        double norm = 0, sum = 0, support = 0;
        for (const auto &t : tokens) {
            const auto v = idf(t);
            norm += v * v;
            if (auto it = signed_terms.find(t); it != signed_terms.end())
                sum += it->second;
            if (auto it = absolute_terms.find(t); it != absolute_terms.end())
                support += it->second;
        }
        if (norm == 0)
            return 0.5;
        return std::clamp(0.5 + 0.5 * sum / (support + 4 * std::sqrt(norm)), 0.0, 1.0);
    }
};
} // namespace
double title_match(const Profile &p, const models::Clip &c) { return TextProfile(p).match(c.title); }
void score(std::vector<Candidate> &cs, const Profile &p, Surface surface) {
    const TextProfile text(p);
    const double confidence = std::min(1.0, p.interactions.size() / 30.0);
    // Percentile external popularity within language/category/age cohort in the
    // retrieved pool.
    std::map<std::string, std::vector<double>> cohorts;
    auto key = [](const Candidate &c) {
        return c.clip.broadcaster_lang + ":" + std::to_string(c.clip.game_id) + ":" +
               std::to_string(c.age_hours < 24    ? 0
                              : c.age_hours < 168 ? 1
                              : c.age_hours < 720 ? 2
                                                  : 3);
    };
    std::vector<double> all;
    for (const auto &c : cs) {
        cohorts[key(c)].push_back(c.clip.view_count);
        all.push_back(c.clip.view_count);
    }
    for (auto &[_, values] : cohorts)
        std::sort(values.begin(), values.end());
    std::sort(all.begin(), all.end());
    for (auto &c : cs) {
        const auto &local = cohorts[key(c)];
        const auto &v = local.size() >= 10 ? local : all;
        const double popularity =
            v.empty() ? 0.5
                      : (std::lower_bound(v.begin(), v.end(), c.clip.view_count) - v.begin() +
                         std::upper_bound(v.begin(), v.end(), c.clip.view_count) - v.begin()) /
                            (2.0 * v.size());
        const double twitch = 0.65 * popularity + 0.35 * bounded_log(c.twitch_velocity, 10000);
        const double personal = personal_match(p, c.clip);
        c.followed = std::find(p.followed_channels.begin(), p.followed_channels.end(),
                               c.clip.broadcaster_id) != p.followed_channels.end();
        c.score = (0.05 + 0.25 * confidence) * personal + (0.40 - 0.15 * confidence) * audience_quality(c) +
                  (0.30 - 0.18 * confidence) * twitch +
                  0.20 * confidence * (0.5 + 0.5 * std::clamp(c.collaborative, -1.0, 1.0)) +
                  (0.05 + 0.03 * confidence) * text.match(c.clip.title) +
                  (0.20 - 0.15 * confidence) * std::exp(-std::max(0.0, c.age_hours) / 168.0) -
                  (c.recently_opened > 0 ? 0.20 : 0.0);
        c.novel =
            approval(c.likes, c.dislikes) >= 0.65 && !c.seen_before &&
            std::none_of(p.interactions.begin(), p.interactions.end(),
                         [&](const auto &i) { return i.clip.broadcaster_id == c.clip.broadcaster_id; }) &&
            !c.followed;
        if (surface == Surface::Rush) {
            // Rush emphasizes individual taste and unknown content over global
            // popularity.
            c.score = (0.10 + 0.35 * confidence) * personal +
                      (0.30 - 0.15 * confidence) * audience_quality(c) + (0.25 - 0.20 * confidence) * twitch +
                      0.20 * confidence * (0.5 + 0.5 * std::clamp(c.collaborative, -1.0, 1.0)) +
                      0.10 * text.match(c.clip.title) +
                      (0.25 - 0.20 * confidence) * std::exp(-std::max(0.0, c.age_hours) / 168.0) -
                      (c.seen_before ? 0.30 : 0.0);
        }
        if (!c.clip.promo_label.empty())
            c.score += 0.02;
    }
}
std::vector<Candidate> select(std::vector<Candidate> cs, size_t limit, uint64_t seed, size_t offset,
                              Surface surface) {
    // Gumbel top-k samples without replacement, weighted by relevance. Compute
    // once per candidate: the comparator must remain pure and inexpensive.
    // The queue persists this order; only a new home session changes the draw.
    std::unordered_map<uint64_t, double> priorities;
    if (surface == Surface::Home && seed != 0) {
        priorities.reserve(cs.size());
        for (const auto &c : cs) {
            // Midpoints keep U strictly inside (0,1), including extreme hashes.
            const double u =
                    ((mix(c.clip.id ^ seed) >> 12) + 0.5) / 4503599627370496.0;
            priorities.emplace(c.clip.id, c.score - 0.08 * std::log(-std::log(u)));
        }
    }
    std::stable_sort(cs.begin(), cs.end(), [&](const auto &a, const auto &b) {
        if (!priorities.empty()) {
            const auto pa = priorities.at(a.clip.id), pb = priorities.at(b.clip.id);
            if (pa != pb)
                return pa > pb;
        }
        if (a.score != b.score)
            return a.score > b.score;
        return mix(a.clip.id ^ seed) > mix(b.clip.id ^ seed);
    });
    // Discovery rotation uses the session seed, rather than favoring the same
    // newest IDs forever.
    std::vector<size_t> discovery;
    for (size_t i = 0; i < cs.size(); ++i)
        if (cs[i].discovery || (surface == Surface::Rush && cs[i].novel))
            discovery.push_back(i);
    std::sort(discovery.begin(), discovery.end(), [&](size_t a, size_t b) {
        auto priority = [&](size_t i) {
            const double random = (mix(cs[i].clip.id ^ seed) >> 11) / 9007199254740992.0;
            return (surface == Surface::Rush ? 0.75 : 0.35) * cs[i].score +
                   (surface == Surface::Rush ? 0.25 : 0.65) * random;
        };
        return priority(a) > priority(b);
    });
    std::vector<Candidate> out;
    std::vector<bool> used(cs.size());
    while (out.size() < limit) {
        const size_t position = offset + out.size();
        const bool want_old = position % 10 == 7;
        const bool want_new = surface == Surface::Rush
                                  ? (position % 10 == 1 || position % 10 == 4 || position % 10 == 8)
                                  : position % 10 == 3;
        const bool want_novel = surface == Surface::Rush && position % 10 == 6;
        const bool want_followed = surface == Surface::Home &&
                                   (position % 10 == 1 || position % 10 == 6);
        size_t chosen = cs.size();
        for (unsigned pass = 0; pass < 4 && chosen == cs.size(); ++pass) {
            for (size_t k = 0; k < cs.size(); ++k) {
                const size_t i = want_new && pass < 2 && k < discovery.size() ? discovery[k] : k;
                if (used[i])
                    continue;
                if (pass < 2 &&
                    ((want_old && !cs[i].evergreen) ||
                     (want_new && (!cs[i].discovery || cs[i].seen_before)) ||
                     (want_novel && !cs[i].novel) || (want_followed && !cs[i].followed)))
                    continue;
                if (std::any_of(out.begin(), out.end(),
                                [&](const auto &x) { return same_moment(x.clip, cs[i].clip); }))
                    continue;
                if (pass % 2 == 0 && !out.empty()) {
                    size_t count = 0;
                    for (size_t n = out.size() > 9 ? out.size() - 9 : 0; n < out.size(); ++n)
                        if (!cs[i].clip.broadcaster_id.empty() &&
                            out[n].clip.broadcaster_id == cs[i].clip.broadcaster_id)
                            ++count;
                    if ((!cs[i].clip.broadcaster_id.empty() &&
                         out.back().clip.broadcaster_id == cs[i].clip.broadcaster_id) ||
                        count >= 2)
                        continue;
                }
                chosen = i;
                break;
            }
        }
        if (chosen == cs.size())
            break;
        used[chosen] = true;
        out.push_back(std::move(cs[chosen]));
    }
    return out;
}
} // namespace clipstream::services::recommendations
