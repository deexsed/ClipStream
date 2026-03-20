#pragma once

#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace clipstream::utils {

inline std::string trim_copy(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

/// FNV-1a 64 → 16 hex: кластер похожих заголовков в диверсификации ленты
inline std::string compute_title_fingerprint(const std::string& title, const std::string& broadcaster_name) {
    std::string t = trim_copy(title);
    std::string lower;
    lower.reserve(t.size());
    for (unsigned char c : t) lower.push_back(static_cast<char>(std::tolower(c)));
    const std::string payload = lower + "|" + broadcaster_name;

    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : payload) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << h;
    return oss.str();
}

} // namespace clipstream::utils
