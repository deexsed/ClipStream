#pragma once
#include <algorithm>
#include <string>
namespace clipstream::utils {
inline std::string utf8_prefix(const std::string &text, size_t bytes) {
    size_t end = std::min(bytes, text.size());
    while (end > 0 && end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
        --end;
    return text.substr(0, end);
}
} // namespace clipstream::utils
