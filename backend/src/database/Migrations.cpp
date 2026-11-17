#include "database/Migrations.hpp"
#include "database/SqlEscape.hpp"
#include "utils/Crypto.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/sha.h>
#include <optional>
#include <regex>
#include <sstream>
namespace clipstream::database {
namespace {
constexpr unsigned expected_version = 13;
std::vector<std::string> statements(const std::string &input) {
    std::string clean;
    std::istringstream lines(input);
    std::string line;
    while (std::getline(lines, line)) {
        auto start = line.find_first_not_of(" \t\r");
        if (start != std::string::npos && line.substr(start, 2) == "--")
            continue;
        clean += line;
        clean += char(10);
    }
    std::vector<std::string> out;
    std::string sql;
    bool quoted = false;
    for (size_t i = 0; i < clean.size(); ++i) {
        char c = clean[i];
        if (c == 39 && (i == 0 || clean[i - 1] != 92))
            quoted = !quoted;
        if (c == ';' && !quoted) {
            if (sql.find_first_not_of(" \n\r\t") != std::string::npos)
                out.push_back(sql);
            sql.clear();
        } else
            sql += c;
    }
    if (sql.find_first_not_of(" \n\r\t") != std::string::npos)
        out.push_back(sql);
    return out;
}
bool exists(MYSQL *c, const std::string &table, const std::string &name, bool index) {
    const std::string sql = "SELECT 1 FROM information_schema." +
                            std::string(index ? "STATISTICS" : "COLUMNS") +
                            " WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='" + escape(c, table) + "' AND " +
                            (index ? "INDEX_NAME" : "COLUMN_NAME") + "='" + escape(c, name) + "' LIMIT 1";
    query(c, sql.c_str());
    Result result(mysql_store_result(c));
    return result && mysql_fetch_row(result);
}
struct Addition {
    std::string table, name, clause;
    bool index;
};
std::optional<Addition> addition(const std::string &sql) {
    static const std::regex pattern(R"(^\s*ALTER\s+TABLE\s+(\w+)\s+(ADD\s+(COLUMN|INDEX)\s+(\w+)[\s\S]*)$)",
                                    std::regex::icase);
    std::smatch match;
    if (!std::regex_match(sql, match, pattern))
        return std::nullopt;
    auto kind = match[3].str();
    std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char c) { return std::toupper(c); });
    return Addition{match[1], match[4], match[2], kind == "INDEX"};
}
void apply_statements(MYSQL *c, const std::vector<std::string> &script, const std::string &filename) {
    for (size_t position = 0; position < script.size();) {
        const auto first = position;
        try {
            const auto head = addition(script[position]);
            if (!head) {
                query(c, script[position++].c_str());
                continue;
            }
            // Preserve statement order and all DDL/DML boundaries. A single ALTER avoids repeated
            // table rebuilds/fsyncs; existing pieces are filtered individually for interrupted upgrades.
            std::string combined = "ALTER TABLE " + head->table + " ";
            size_t pending = 0;
            while (position < script.size()) {
                const auto next = addition(script[position]);
                if (!next || next->table != head->table)
                    break;
                if (!exists(c, next->table, next->name, next->index)) {
                    if (pending++)
                        combined += ", ";
                    combined += next->clause;
                }
                ++position;
            }
            if (pending)
                query(c, combined.c_str());
        } catch (const Error &error) {
            throw std::runtime_error("Migration " + filename + " statements " + std::to_string(first + 1) +
                                     "-" + std::to_string(std::max(position, first + 1)) +
                                     " failed (database code " + std::to_string(error.code) + ")");
        }
    }
}
} // namespace
void migrate(ConnectionPool &pool, const std::string &directory) {
    ScopedConnection conn(pool);
    auto c = conn.raw();
    query(c, "SELECT GET_LOCK(CONCAT(DATABASE(),':schema'),30)");
    Result lock(mysql_store_result(c));
    auto row = mysql_fetch_row(lock);
    if (!row || !row[0] || std::string(row[0]) != "1")
        throw std::runtime_error("Schema migration lock unavailable");
    lock.reset();
    struct Unlock {
        MYSQL *c;
        ~Unlock() { mysql_query(c, "DO RELEASE_LOCK(CONCAT(DATABASE(),':schema'))"); }
    } unlock{c};
    query(c, "CREATE TABLE IF NOT EXISTS schema_migrations(version INT UNSIGNED PRIMARY KEY, checksum "
             "CHAR(64) NOT NULL, applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP)");
    query(c, "SELECT COALESCE(MAX(version),0) FROM schema_migrations");
    Result version_result(mysql_store_result(c));
    auto version_row = mysql_fetch_row(version_result);
    if (version_row && version_row[0] && std::stoul(version_row[0]) > expected_version)
        throw std::runtime_error("Database schema is newer than this application");
    version_result.reset();
    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
        if (entry.path().extension() == ".sql")
            paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    if (paths.size() != expected_version)
        throw std::runtime_error("Migration scripts missing or unexpected");
    for (const auto &path : paths) {
        unsigned version = std::stoul(path.filename().string().substr(0, 3));
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("Migration file unreadable");
        std::string sql((std::istreambuf_iterator<char>(file)), {});
        // Canonical content makes the same Git migration portable between Windows and Linux.
        for (size_t cr = sql.find("\r\n"); cr != std::string::npos; cr = sql.find("\r\n", cr))
            sql.erase(cr, 1);
        if (sql.starts_with("\xEF\xBB\xBF"))
            sql.erase(0, 3);
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char *>(sql.data()), sql.size(), digest);
        auto checksum = utils::hex(digest, sizeof digest);
        auto check = "SELECT checksum FROM schema_migrations WHERE version=" + std::to_string(version);
        query(c, check.c_str());
        Result applied(mysql_store_result(c));
        auto previous = mysql_fetch_row(applied);
        if (previous) {
            if (!previous[0] || checksum != previous[0])
                throw std::runtime_error("Applied migration checksum mismatch");
            continue;
        }
        applied.reset();
        apply_statements(c, statements(sql), path.filename().string());
        auto record = "INSERT INTO schema_migrations(version,checksum) VALUES(" + std::to_string(version) +
                      ",'" + checksum + "')";
        query(c, record.c_str());
        std::cout << "[Schema] Applied " << path.filename().string() << "\n";
    }
}
bool schema_ready(ConnectionPool &pool) {
    ScopedConnection conn(pool);
    query(conn.raw(),
          "SELECT COUNT(*),COALESCE(MIN(version),0),COALESCE(MAX(version),0) FROM schema_migrations");
    Result result(mysql_store_result(conn.raw()));
    auto row = mysql_fetch_row(result);
    return row && row[0] && std::stoul(row[0]) == expected_version && std::stoul(row[1]) == 1 &&
           std::stoul(row[2]) == expected_version;
}
} // namespace clipstream::database
