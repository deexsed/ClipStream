#pragma once
#include "database/ConnectionPool.hpp"
#include <cstdint>
#include <string>

namespace clipstream::repositories {
// Called inside the owning mutation transaction. Dirty generations survive concurrent recomputes.
inline void mark_recommendation_dirty(MYSQL *conn, uint64_t user, uint64_t clip) {
    database::query(conn, ("INSERT INTO recommendation_dirty_clips(clip_id) VALUES(" + std::to_string(clip) +
                           ") ON DUPLICATE KEY UPDATE generation=generation+1")
                              .c_str());
    if (user)
        database::query(conn, ("INSERT INTO recommendation_active_users(user_id) VALUES(" +
                               std::to_string(user) + ") ON DUPLICATE KEY UPDATE touched_at=UTC_TIMESTAMP()")
                                  .c_str());
}
} // namespace clipstream::repositories
