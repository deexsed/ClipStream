#include "server/HttpServer.hpp"
#include "server/HttpSession.hpp"
#include <iostream>

namespace clipstream::server {

HttpServer::HttpServer(asio::io_context &ioc, uint16_t port, Router &router, BlockingExecutor &executor)
    : ioc_(ioc), acceptor_(ioc, tcp::endpoint(tcp::v4(), port)), router_(router), executor_(executor) {
    acceptor_.set_option(asio::socket_base::reuse_address(true));
}

void HttpServer::start() {
    std::cout << "[Server] Listening on port " << acceptor_.local_endpoint().port() << "\n";
    do_accept();
}

void HttpServer::stop() {
    beast::error_code ec;
    acceptor_.cancel(ec);
    acceptor_.close(ec);
}

void HttpServer::do_accept() {
    if (!acceptor_.is_open())
        return;
    acceptor_.async_accept(asio::make_strand(ioc_), [this](boost::system::error_code ec, tcp::socket socket) {
        if (!ec) {
            std::make_shared<HttpSession>(std::move(socket), router_, executor_)->start();
        } else {
            std::cerr << "[Server] Accept error: " << ec.message() << "\n";
        }
        do_accept();
    });
}

} // namespace clipstream::server
