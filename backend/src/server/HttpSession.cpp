#include "server/HttpSession.hpp"
#include <iostream>

namespace clipstream::server {

HttpSession::HttpSession(tcp::socket socket, Router& router)
    : socket_(std::move(socket)), router_(router) {}

void HttpSession::start() { do_read(); }

void HttpSession::do_read() {
    auto self = shared_from_this();
    http::async_read(socket_, buffer_, request_,
        [self](beast::error_code ec, std::size_t bytes) {
            self->on_read(ec, bytes);
        });
}

void HttpSession::on_read(beast::error_code ec, std::size_t /*bytes*/) {
    if (ec == http::error::end_of_stream) {
        beast::error_code ignored_ec;
        socket_.shutdown(tcp::socket::shutdown_send, ignored_ec);
        return;
    }
    if (ec) {
        std::cerr << "[Session] Read error: " << ec.message() << "\n";
        return;
    }

    // CORS preflight
    if (request_.method() == http::verb::options) {
        http::response<http::string_body> res{http::status::no_content, request_.version()};
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_methods, "GET, POST, PUT, DELETE, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
        res.set(http::field::access_control_max_age, "86400");
        res.prepare_payload();
        do_write(std::move(res));
        return;
    }

    std::string auth_header;
    if (request_.count(http::field::authorization))
        auth_header = std::string(request_[http::field::authorization]);

    auto api_res = router_.dispatch(
        request_.method(),
        std::string(request_.target()),
        request_.body(),
        auth_header
    );

    http::response<http::string_body> http_res{
        static_cast<http::status>(api_res.status), request_.version()};
    http_res.set(http::field::content_type, "application/json");
    http_res.set(http::field::access_control_allow_origin, "*");
    http_res.body() = api_res.body.dump();
    http_res.prepare_payload();
    http_res.keep_alive(request_.keep_alive());

    do_write(std::move(http_res));
}

void HttpSession::do_write(http::response<http::string_body> res) {
    auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));
    auto self = shared_from_this();
    http::async_write(socket_, *sp,
        [self, sp](beast::error_code ec, std::size_t bytes) {
            self->on_write(ec, bytes, sp->need_eof());
        });
}

void HttpSession::on_write(beast::error_code ec, std::size_t /*bytes*/, bool close) {
    if (ec) {
        std::cerr << "[Session] Write error: " << ec.message() << "\n";
        return;
    }
    if (close) {
        beast::error_code ignored_ec;
        socket_.shutdown(tcp::socket::shutdown_send, ignored_ec);
        return;
    }
    request_ = {};
    buffer_.consume(buffer_.size());
    do_read();
}

} // namespace clipstream::server
