#pragma once
#include <array>
#include <charconv>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdexcept>
#include <string>
#include <string_view>
namespace clipstream::utils {
inline std::string hex(const unsigned char *bytes, size_t count) {
    const char *chars = "0123456789abcdef";
    std::string out(count * 2, '0');
    for (size_t i = 0; i < count; ++i) {
        out[i * 2] = chars[bytes[i] >> 4];
        out[i * 2 + 1] = chars[bytes[i] & 15];
    }
    return out;
}
inline bool unhex(std::string_view input, unsigned char *output, size_t size) {
    if (input.size() != size * 2)
        return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i < size; ++i) {
        int a = nibble(input[i * 2]), b = nibble(input[i * 2 + 1]);
        if (a < 0 || b < 0)
            return false;
        output[i] = static_cast<unsigned char>(a * 16 + b);
    }
    return true;
}
} // namespace clipstream::utils
