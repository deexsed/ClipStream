#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <mysql/mysql.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace clipstream::database {
class Error : public std::runtime_error {
  public:
    explicit Error(unsigned code)
        : std::runtime_error("Database operation failed (" + std::to_string(code) + ")"), code(code) {}
    unsigned code;
};
inline int query(MYSQL *conn, const char *sql) {
    if (mysql_query(conn, sql) != 0)
        throw Error(mysql_errno(conn));
    return 0;
}
// Retry only errors for which the server guarantees the transaction can be rolled back.
// Connection loss/unknown COMMIT outcomes are deliberately never retried.
template <class Operation> auto retry_transaction(Operation &&operation) -> decltype(operation()) {
    for (unsigned attempt = 0;; ++attempt) {
        try {
            return operation();
        } catch (const Error &error) {
            if ((error.code != 1213 && error.code != 1205) || attempt >= 3)
                throw;
            std::this_thread::sleep_for(std::chrono::milliseconds(2u << attempt));
        }
    }
}
class Result {
  public:
    explicit Result(MYSQL_RES *value = nullptr) : value_(value) {}
    ~Result() { reset(); }
    Result(const Result &) = delete;
    Result &operator=(const Result &) = delete;
    Result(Result &&other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Result &operator=(Result &&other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    operator MYSQL_RES *() const noexcept { return value_; }
    void reset() noexcept {
        if (value_)
            mysql_free_result(std::exchange(value_, nullptr));
    }

  private:
    MYSQL_RES *value_;
};
class Transaction {
  public:
    explicit Transaction(MYSQL *conn) : conn_(conn) { query(conn_, "START TRANSACTION"); }
    ~Transaction() {
        if (!committed_)
            mysql_rollback(conn_);
    }
    void commit() {
        query(conn_, "COMMIT");
        committed_ = true;
    }

  private:
    MYSQL *conn_;
    bool committed_ = false;
};

struct DbConfig {
    std::string host;
    uint16_t port{3306};
    std::string database;
    std::string user;
    std::string password;
    size_t pool_size{10};
    std::chrono::milliseconds acquire_timeout{2000};
    unsigned read_timeout_seconds{5};
    unsigned write_timeout_seconds{5};
};

class MysqlConnection {
  public:
    explicit MysqlConnection(const DbConfig &cfg);
    ~MysqlConnection();

    MysqlConnection(const MysqlConnection &) = delete;
    MysqlConnection &operator=(const MysqlConnection &) = delete;

    MYSQL *raw() noexcept { return conn_; }
    bool ping();

  private:
    MYSQL *conn_{nullptr};
};

class ConnectionPool {
  public:
    explicit ConnectionPool(const DbConfig &cfg);
    ~ConnectionPool() = default;

    ConnectionPool(const ConnectionPool &) = delete;
    ConnectionPool &operator=(const ConnectionPool &) = delete;

    std::shared_ptr<MysqlConnection> acquire();
    void release(std::shared_ptr<MysqlConnection> conn);

  private:
    DbConfig cfg_;
    std::vector<std::shared_ptr<MysqlConnection>> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// RAII-обёртка для автоматического возврата соединения в пул
class ScopedConnection {
  public:
    ScopedConnection(ConnectionPool &pool) : pool_(pool), conn_(pool.acquire()) {}

    ~ScopedConnection() { release(); }

    ScopedConnection(const ScopedConnection &) = delete;
    ScopedConnection &operator=(const ScopedConnection &) = delete;

    MYSQL *raw() const {
        if (!conn_)
            throw std::logic_error("Connection already released");
        return conn_->raw();
    }
    void release() {
        if (conn_)
            pool_.release(std::exchange(conn_, {}));
    }

  private:
    ConnectionPool &pool_;
    std::shared_ptr<MysqlConnection> conn_;
};

} // namespace clipstream::database
