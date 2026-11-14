#pragma once
#include "database/ConnectionPool.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>

namespace clipstream::repositories {
class AdsRepository {
public:
    explicit AdsRepository(database::ConnectionPool &pool) : pool_(pool) {}
    nlohmann::json list(unsigned limit, unsigned offset);
    nlohmann::json metrics();
    std::optional<nlohmann::json> find(uint64_t id);
    uint64_t create(const nlohmann::json &campaign);
    bool update(uint64_t id, const nlohmann::json &campaign);
    bool remove(uint64_t id);
    // Reservation and cap accounting are committed atomically across API replicas.
    nlohmann::json serve(const std::string &placement, const std::string &audience_hash,
                         const std::string &token_hash, const std::set<std::string> &media_hosts);
    std::optional<nlohmann::json> event(const std::string &token_hash, const std::string &event);
private:
    database::ConnectionPool &pool_;
};
} // namespace clipstream::repositories
