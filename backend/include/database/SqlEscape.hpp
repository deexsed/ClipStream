#pragma once

#include <string>
#include <mysql/mysql.h>

namespace clipstream::database {

inline std::string escape(MYSQL* conn, const std::string& input) {
    std::string buf(input.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(conn, buf.data(), input.data(),
                                                  static_cast<unsigned long>(input.size()));
    buf.resize(len);
    return buf;
}

} // namespace clipstream::database
