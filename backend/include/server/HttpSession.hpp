#pragma once

#include <memory>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include "server/Router.hpp"

namespace clipstream::server {

namespace beast = boost::beast;
namespace asio  = boost::asio;
using tcp = asio::ip::tcp;

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket, Router& router);
    void start();

private:
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes);
    void do_write(http::response<http::string_body> res);
    void on_write(beast::error_code ec, std::size_t bytes, bool close);

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    Router& router_;
};

} // namespace clipstream::server
