#pragma once

#include <cstdint>
#include <boost/asio.hpp>
#include "server/Router.hpp"

namespace clipstream::server {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class HttpServer {
public:
    HttpServer(asio::io_context& ioc, uint16_t port, Router& router);
    void start();

private:
    void do_accept();

    asio::io_context& ioc_;
    tcp::acceptor acceptor_;
    Router& router_;
};

} // namespace clipstream::server
