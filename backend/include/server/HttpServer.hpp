#pragma once

#include "server/BlockingExecutor.hpp"
#include "server/Router.hpp"
#include <boost/asio.hpp>
#include <cstdint>

namespace clipstream::server {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class HttpServer {
  public:
    HttpServer(asio::io_context &ioc, uint16_t port, Router &router, BlockingExecutor &executor);
    void start();
    void stop();

  private:
    void do_accept();

    asio::io_context &ioc_;
    tcp::acceptor acceptor_;
    Router &router_;
    BlockingExecutor &executor_;
};

} // namespace clipstream::server
