#include "services/RecommendationScoring.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace clipstream::services::recommendations;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
static Candidate make(uint64_t id, double age = 48) {
    Candidate c;
    c.clip.id = id;
    c.clip.game_id = 1;
    c.clip.broadcaster_id = std::to_string(id);
    c.clip.title = "Tournament final " + std::to_string(id);
    c.clip.duration = 30;
    c.age_hours = age;
    return c;
}
int main() {
    try {
        check(approval(2, 0) < approval(100, 0), "Small samples must remain uncertain");
        check(approval(100, 100) < approval(2, 0), "Vote balance must outweigh raw like count");
        Candidate good = make(1);
        good.likes = 100;
        good.opens = 300;
        Candidate bad = good;
        bad.dislikes = 100;
        check(audience_quality(bad) < audience_quality(good), "Dislikes must lower audience quality");
        Profile p;
        p.interactions.push_back({good.clip, 3, 0});
        auto related = make(2);
        related.clip.broadcaster_id = good.clip.broadcaster_id;
        auto unknown = make(3);
        unknown.clip.game_id = 2;
        check(personal_match(p, related.clip) > personal_match(p, unknown.clip), "Personal preference lost");
        const auto positive = personal_match(p, related.clip);
        p.interactions[0].weight = -4;
        check(personal_match(p, related.clip) < 0.5, "Negative personal signal ignored");
        p.interactions[0].weight = 3;
        p.interactions[0].age_days = 180;
        check(personal_match(p, related.clip) < positive, "Old interactions must decay");
        p.interactions = {{good.clip, 3, 0}};
        p.interactions[0].clip.title = "ФИНАЛ ТУРНИРА";
        related.clip.title = "финал турнира";
        check(title_match(p, related.clip) > title_match(p, unknown.clip), "Cyrillic text similarity failed");
        std::vector<Candidate> cs;
        for (uint64_t i = 1; i <= 40; ++i) {
            auto c = make(i);
            c.score = 1.0 - i / 1000.0;
            if (i >= 20 && i <= 25) {
                c.evergreen = true;
                c.age_hours = 10000;
            }
            if (i >= 26 && i <= 35)
                c.discovery = true;
            if (i >= 36)
                c.novel = true;
            cs.push_back(c);
        }
        auto feed = select(cs, 20, 42);
        check(feed[3].discovery && feed[13].discovery, "New low-popularity allocation missing");
        check(feed[7].evergreen && feed[17].evergreen, "Old popular allocation missing");
        auto rush = select(cs, 20, 42, 0, Surface::Rush);
        check(rush[1].discovery && rush[4].discovery && rush[8].discovery,
              "Rush exploration allocation missing");
        check(rush[6].novel, "Rush must discover new channels");
        auto repeated = select(cs, 20, 42);
        for (size_t i = 0; i < feed.size(); ++i)
            check(feed[i].clip.id == repeated[i].clip.id, "Unstable session ranking");
        bool changed = false;
        auto rotated = select(cs, 20, 12);
        for (size_t i = 0; i < feed.size(); ++i)
            changed |= feed[i].clip.id != rotated[i].clip.id;
        check(changed, "Exploration must rotate across sessions");
        // Ordinary positions cannot consume the only reserved candidates ahead of
        // their slots.
        std::vector<Candidate> tiny{make(1), make(2), make(3)};
        tiny[1].clip.vod_id = "vod";
        tiny[1].clip.vod_offset = 100;
        tiny[2].clip.vod_id = "vod";
        tiny[2].clip.vod_offset = 102;
        check(select(tiny, 20).size() == 2, "Duplicate moments must be suppressed");
        for (auto &c : cs)
            c.collaborative = 0;
        for (int i = 0; i < 30; ++i)
            p.interactions.push_back({make(100 + i).clip, 0.3, 0});
        auto target = make(60);
        target.collaborative = 0.8;
        auto negative = target;
        negative.clip.id = 61;
        negative.collaborative = -0.8;
        std::vector<Candidate> pair{target, negative};
        score(pair, p);
        check(pair[0].score > pair[1].score, "Neighbors' negative and positive votes must affect ranking");
        std::vector<Candidate> empty;
        score(empty, {});
        check(select(empty, 20).empty(), "Empty catalog broken");
        const auto started = std::chrono::steady_clock::now();
        std::vector<Candidate> benchmark;
        for (uint64_t i = 0; i < 1700; ++i)
            benchmark.push_back(make(10000 + i));
        Profile benchmark_profile;
        for (uint64_t i = 0; i < 200; ++i)
            benchmark_profile.interactions.push_back({make(10000 + i).clip, 3, 0});
        score(benchmark, benchmark_profile, Surface::Rush);
        check(select(std::move(benchmark), 200, 42, 0, Surface::Rush).size() == 200,
              "Bounded pool selection failed");
        std::cout << "Recommendation contracts passed (1700 candidates / 200 interactions / 200 slots: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                           started)
                         .count()
                  << " ms)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
