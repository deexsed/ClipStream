#pragma once

#include "server/BlockingExecutor.hpp"
#include "server/Router.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <optional>

namespace clipstream::server {

namespace beast = boost::beast;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class HttpSession : public std::enable_shared_from_this<HttpSession> {
  public:
    HttpSession(tcp::socket socket, Router &router, BlockingExecutor &executor);
    void start();

  private:
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes);
    void do_write(http::response<http::string_body> res);
    void on_write(beast::error_code ec, std::size_t bytes, bool close);

    beast::tcp_stream stream_;
    BlockingExecutor &executor_;
    std::optional<http::request_parser<http::string_body>> parser_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    Router &router_;
};

} // namespace clipstream::server
