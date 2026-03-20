#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace clipstream::repositories {

template <typename T>
class IRepository {
public:
    virtual ~IRepository() = default;
    virtual std::optional<T> find_by_id(uint64_t id) = 0;
    virtual std::vector<T> find_all(uint32_t limit, uint32_t offset) = 0;
    virtual uint64_t create(const T& entity) = 0;
    virtual bool update(const T& entity) = 0;
    virtual bool remove(uint64_t id) = 0;
};

} // namespace clipstream::repositories
