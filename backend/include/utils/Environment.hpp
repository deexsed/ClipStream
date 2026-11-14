#pragma once
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
namespace clipstream::utils {
inline std::string env(const char *name, const std::string &fallback = "") {
    const std::string filename = std::string(name) + "_FILE";
    if (const char *file = std::getenv(filename.c_str()); file && *file) {
        std::ifstream input(file, std::ios::binary);
        if (!input)
            throw std::runtime_error("Secret file unreadable: " + filename);
        std::string value((std::istreambuf_iterator<char>(input)), {});
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
            value.pop_back();
        return value;
    }
    const char *value = std::getenv(name);
    return value ? std::string(value) : fallback;
}
} // namespace clipstream::utils
