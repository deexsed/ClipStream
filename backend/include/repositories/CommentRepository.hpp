#pragma once

#include "repositories/IRepository.hpp"
#include "models/Comment.hpp"
#include "database/ConnectionPool.hpp"
#include <string>

namespace clipstream::repositories {

class CommentRepository : public IRepository<models::Comment> {
public:
    explicit CommentRepository(database::ConnectionPool& pool);

    std::optional<models::Comment> find_by_id(uint64_t id) override;
    std::vector<models::Comment>   find_all(uint32_t limit, uint32_t offset) override;
    uint64_t                       create(const models::Comment& comment) override;
    bool                           update(const models::Comment& comment) override;
    bool                           remove(uint64_t id) override;

    std::vector<models::Comment> find_by_clip(uint64_t clip_id, uint32_t limit, uint32_t offset,
                                              const std::string& sort, uint64_t viewer_user_id);
    std::vector<models::Comment> find_replies(uint64_t parent_id, uint64_t viewer_user_id);
    bool                         set_comment_vote(uint64_t user_id, uint64_t comment_id, int vote);

private:
    models::Comment row_to_comment(MYSQL_ROW row, MYSQL_RES* res);
    database::ConnectionPool& pool_;
};

} // namespace clipstream::repositories
