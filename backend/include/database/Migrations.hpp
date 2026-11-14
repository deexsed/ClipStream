#pragma once
#include "database/ConnectionPool.hpp"
#include <string>
namespace clipstream::database {
void migrate(ConnectionPool &pool, const std::string &directory);
bool schema_ready(ConnectionPool &pool);
} // namespace clipstream::database
