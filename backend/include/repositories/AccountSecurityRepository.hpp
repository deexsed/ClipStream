#pragma once
#include "database/ConnectionPool.hpp"
#include <nlohmann/json.hpp>
#include <optional>
namespace clipstream::repositories {
class AccountSecurityRepository {
  public:
    explicit AccountSecurityRepository(database::ConnectionPool &pool) : pool_(pool) {}
    bool bootstrap_pending();
    bool bootstrap_admin(uint64_t user_id, const std::string &expected_twitch_id);
    bool is_ip_banned(const std::string &ip);
    void restrict_user(uint64_t actor, uint64_t target, const std::string &action, unsigned hours,
                       const std::string &reason);
    nlohmann::json user_state(uint64_t actor, uint64_t target);
    nlohmann::json ip_bans(uint64_t actor, uint32_t limit, uint32_t offset);
    uint64_t ban_ip(uint64_t actor, const std::string &ip, unsigned hours, const std::string &reason,
                    const std::string &actor_ip);
    void unban_ip(uint64_t actor, uint64_t id);
    nlohmann::json audit(uint64_t actor, uint32_t limit, uint32_t offset);
    void resolve_report(uint64_t actor, uint64_t report, const std::string &resolution,
                        const std::string &reason);
    void set_clip_visibility(uint64_t actor, uint64_t clip, bool hidden, const std::string &reason);
    void classify_clip(uint64_t actor, uint64_t clip, bool mature, const std::string &reason);

  private:
    database::ConnectionPool &pool_;
};
} // namespace clipstream::repositories
