#pragma once

#include "repositories/IRepository.hpp"
#include "models/Comment.hpp"
#include "database/ConnectionPool.hpp"

namespace clipstream::repositories {

class CommentRepository : public IRepository<models::Comment> {
public:
    explicit CommentRepository(database::ConnectionPool& pool);

    std::optional<models::Comment> find_by_id(uint64_t id) override;
    std::vector<models::Comment>   find_all(uint32_t limit, uint32_t offset) override;
    uint64_t                       create(const models::Comment& comment) override;
    bool                           update(const models::Comment& comment) override;
    bool                           remove(uint64_t id) override;

    std::vector<models::Comment> find_by_clip(uint64_t clip_id, uint32_t limit, uint32_t offset);
    std::vector<models::Comment> find_replies(uint64_t parent_id);

private:
    models::Comment row_to_comment(MYSQL_ROW row, MYSQL_RES* res);
    database::ConnectionPool& pool_;
};

} // namespace clipstream::repositories
