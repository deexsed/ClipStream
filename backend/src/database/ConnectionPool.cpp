#include "database/ConnectionPool.hpp"
#include <stdexcept>
#include <iostream>

namespace clipstream::database {

MysqlConnection::MysqlConnection(const DbConfig& cfg) {
    conn_ = mysql_init(nullptr);
    if (!conn_)
        throw std::runtime_error("mysql_init failed");

    if (!mysql_real_connect(conn_,
            cfg.host.c_str(), cfg.user.c_str(), cfg.password.c_str(),
            cfg.database.c_str(), cfg.port, nullptr, 0))
    {
        std::string err = mysql_error(conn_);
        mysql_close(conn_);
        conn_ = nullptr;
        throw std::runtime_error("MySQL connect failed: " + err);
    }

    mysql_set_character_set(conn_, "utf8mb4");
}

MysqlConnection::~MysqlConnection() {
    if (conn_) mysql_close(conn_);
}

bool MysqlConnection::ping() {
    return conn_ && mysql_ping(conn_) == 0;
}

ConnectionPool::ConnectionPool(const DbConfig& cfg) : cfg_(cfg) {
    for (size_t i = 0; i < cfg_.pool_size; ++i) {
        pool_.push(std::make_shared<MysqlConnection>(cfg_));
    }
    std::cout << "[DB] Pool created with " << cfg_.pool_size << " connections\n";
}

std::shared_ptr<MysqlConnection> ConnectionPool::acquire() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return !pool_.empty(); });

    auto conn = pool_.front();
    pool_.pop();

    if (!conn->ping()) {
        conn = std::make_shared<MysqlConnection>(cfg_);
    }

    return conn;
}

void ConnectionPool::release(std::shared_ptr<MysqlConnection> conn) {
    std::lock_guard lock(mutex_);
    pool_.push(std::move(conn));
    cv_.notify_one();
}

} // namespace clipstream::database
