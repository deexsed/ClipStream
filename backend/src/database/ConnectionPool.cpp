#include "database/ConnectionPool.hpp"
#include "utils/Environment.hpp"
#include <iostream>
#include <stdexcept>

namespace clipstream::database {

namespace {
void initialize_mysql_thread() {
    struct LibraryRuntime {
        LibraryRuntime() {
            if (mysql_library_init(0, nullptr, nullptr) != 0)
                throw Error(2008);
        }
        ~LibraryRuntime() { mysql_library_end(); }
    };
    static LibraryRuntime library;
    struct ThreadRuntime {
        ThreadRuntime() {
            if (mysql_thread_init() != 0)
                throw Error(2008);
        }
        ~ThreadRuntime() { mysql_thread_end(); }
    };
    static thread_local ThreadRuntime thread;
    (void)library;
    (void)thread;
}
} // namespace
MysqlConnection::MysqlConnection(const DbConfig &cfg) {
    initialize_mysql_thread();
    std::unique_ptr<MYSQL, decltype(&mysql_close)> handle(mysql_init(nullptr), mysql_close);
    if (!handle)
        throw Error(2008);
    unsigned timeout = 5;
    mysql_options(handle.get(), MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(handle.get(), MYSQL_OPT_READ_TIMEOUT, &cfg.read_timeout_seconds);
    mysql_options(handle.get(), MYSQL_OPT_WRITE_TIMEOUT, &cfg.write_timeout_seconds);
    const auto ca = utils::env("MYSQL_SSL_CA");
    if (!ca.empty()) {
        if (mysql_options(handle.get(), MYSQL_OPT_SSL_CA, ca.c_str()) != 0)
            throw Error(mysql_errno(handle.get()));
#ifdef MARIADB_BASE_VERSION
        bool verify = true;
        if (mysql_options(handle.get(), MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify) != 0)
            throw Error(mysql_errno(handle.get()));
#else
        enum mysql_ssl_mode mode = SSL_MODE_VERIFY_IDENTITY;
        if (mysql_options(handle.get(), MYSQL_OPT_SSL_MODE, &mode) != 0)
            throw Error(mysql_errno(handle.get()));
#endif
    }
    if (!mysql_real_connect(handle.get(), cfg.host.c_str(), cfg.user.c_str(), cfg.password.c_str(),
                            cfg.database.c_str(), cfg.port, nullptr, 0))
        throw Error(mysql_errno(handle.get()));
    if (mysql_set_character_set(handle.get(), "utf8mb4") != 0)
        throw Error(mysql_errno(handle.get()));
    query(handle.get(), "SET time_zone='+00:00'");
    conn_ = handle.release();
}

MysqlConnection::~MysqlConnection() {
    if (conn_)
        mysql_close(conn_);
}

bool MysqlConnection::ping() { return conn_ && mysql_ping(conn_) == 0; }

ConnectionPool::ConnectionPool(const DbConfig &cfg) : cfg_(cfg) {
    if (!cfg.pool_size || cfg.pool_size > 1024)
        throw std::invalid_argument("Invalid database pool size");
    pool_.reserve(cfg.pool_size);
    for (size_t i = 0; i < cfg_.pool_size; ++i) {
        pool_.push_back(std::make_shared<MysqlConnection>(cfg_));
    }
    std::cout << "[DB] Pool created with " << cfg_.pool_size << " connections\n";
}

std::shared_ptr<MysqlConnection> ConnectionPool::acquire() {
    initialize_mysql_thread();
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, cfg_.acquire_timeout, [this] { return !pool_.empty(); }))
        throw Error(0);
    auto conn = pool_.back();
    pool_.pop_back();
    lock.unlock();
    try {
        if (!conn->ping())
            conn = std::make_shared<MysqlConnection>(cfg_);
    } catch (...) {
        // Keep the slot: a subsequent acquire can reconnect once the database recovers.
        release(conn);
        throw;
    }

    return conn;
}

void ConnectionPool::release(std::shared_ptr<MysqlConnection> conn) {
    std::lock_guard lock(mutex_);
    pool_.push_back(std::move(conn));
    cv_.notify_one();
}

} // namespace clipstream::database
