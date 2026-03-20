#pragma once

#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <condition_variable>
#include <mysql/mysql.h>

namespace clipstream::database {

struct DbConfig {
    std::string host;
    uint16_t    port{3306};
    std::string database;
    std::string user;
    std::string password;
    size_t      pool_size{10};
};

class MysqlConnection {
public:
    explicit MysqlConnection(const DbConfig& cfg);
    ~MysqlConnection();

    MysqlConnection(const MysqlConnection&) = delete;
    MysqlConnection& operator=(const MysqlConnection&) = delete;

    MYSQL* raw() noexcept { return conn_; }
    bool   ping();

private:
    MYSQL* conn_{nullptr};
};

class ConnectionPool {
public:
    explicit ConnectionPool(const DbConfig& cfg);
    ~ConnectionPool() = default;

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    std::shared_ptr<MysqlConnection> acquire();
    void release(std::shared_ptr<MysqlConnection> conn);

private:
    DbConfig cfg_;
    std::queue<std::shared_ptr<MysqlConnection>> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// RAII-обёртка для автоматического возврата соединения в пул
class ScopedConnection {
public:
    ScopedConnection(ConnectionPool& pool)
        : pool_(pool), conn_(pool.acquire()) {}

    ~ScopedConnection() { pool_.release(conn_); }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    MYSQL* raw() noexcept { return conn_->raw(); }

private:
    ConnectionPool& pool_;
    std::shared_ptr<MysqlConnection> conn_;
};

} // namespace clipstream::database
