#include "server/HttpSession.hpp"
#include "utils/Environment.hpp"
#include "utils/Network.hpp"
#include <cstdlib>
#include <iostream>
#include <openssl/crypto.h>

namespace clipstream::server {

HttpSession::HttpSession(tcp::socket socket, Router &router, BlockingExecutor &executor)
    : stream_(std::move(socket)), executor_(executor), router_(router) {}

void HttpSession::start() { do_read(); }

void HttpSession::do_read() {
    auto self = shared_from_this();
    parser_.emplace();
    parser_->body_limit(65536);
    parser_->header_limit(16384);
    stream_.expires_after(std::chrono::seconds(15));
    http::async_read(stream_, buffer_, *parser_,
                     [self](beast::error_code ec, std::size_t bytes) { self->on_read(ec, bytes); });
}

void HttpSession::on_read(beast::error_code ec, std::size_t /*bytes*/) {
    if (ec == http::error::end_of_stream) {
        beast::error_code ignored_ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ignored_ec);
        return;
    }
    if (ec) {
        if (ec == http::error::body_limit || ec == http::error::header_limit) {
            http::response<http::string_body> response{ec == http::error::body_limit
                                                           ? http::status::payload_too_large
                                                           : http::status::request_header_fields_too_large,
                                                       11};
            response.set(http::field::content_type, "application/json");
            response.body() = R"({"error":"Request too large"})";
            response.keep_alive(false);
            response.prepare_payload();
            do_write(std::move(response));
        }
        return;
    }

    request_ = parser_->release();
    parser_.reset();
    if (request_.method() == http::verb::options) {
        http::response<http::string_body> res{http::status::no_content, request_.version()};
        res.prepare_payload();
        do_write(std::move(res));
        return;
    }
    std::string auth_header;
    if (request_.count(http::field::authorization))
        auth_header = std::string(request_[http::field::authorization]);
    beast::error_code address_error;
    auto peer = stream_.socket().remote_endpoint(address_error);
    if (address_error)
        return;
    std::string address = peer.address().to_string();
    address = utils::normalize_ip(address);
    static const std::string proxy_secret = utils::env("PROXY_AUTH_SECRET");
    if (!proxy_secret.empty() && request_.count("X-ClipStream-Proxy-Auth") && request_.count("X-Real-IP")) {
        const auto proof = std::string(request_["X-ClipStream-Proxy-Auth"]);
        if (proof.size() == proxy_secret.size() &&
            CRYPTO_memcmp(proof.data(), proxy_secret.data(), proof.size()) == 0) {
            try {
                address = utils::normalize_ip(std::string(request_["X-Real-IP"]));
            } catch (const std::invalid_argument &) {
                stream_.socket().close();
                return;
            }
        }
    }
    auto self = shared_from_this();
    auto method = request_.method();
    auto target = std::string(request_.target());
    auto body = request_.body();
    auto version = request_.version();
    auto keepalive = request_.keep_alive();
    auto work = [self, method, target = std::move(target), body = std::move(body),
                 auth_header = std::move(auth_header), address = std::move(address), version, keepalive] {
        auto result = self->router_.dispatch(method, target, body, auth_header, address);
        http::response<http::string_body> response{static_cast<http::status>(result.status), version};
        response.set(http::field::content_type, "application/json; charset=utf-8");
        response.set(http::field::cache_control, "no-store");
        response.set("X-Content-Type-Options", "nosniff");
        if (result.body.contains("retry_after"))
            response.set(http::field::retry_after, std::to_string(result.body.value("retry_after", 1)));
        response.body() = result.body.dump();
        response.keep_alive(keepalive);
        response.prepare_payload();
        asio::post(self->stream_.get_executor(),
                   [self, response = std::move(response)]() mutable { self->do_write(std::move(response)); });
    };
    if (!executor_.post(std::move(work))) {
        http::response<http::string_body> res{http::status::service_unavailable, version};
        res.set(http::field::retry_after, "1");
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"error":"Server busy","retry_after":1})";
        res.prepare_payload();
        do_write(std::move(res));
    }
}

void HttpSession::do_write(http::response<http::string_body> res) {
    auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));
    auto self = shared_from_this();
    stream_.expires_after(std::chrono::seconds(15));
    http::async_write(stream_, *sp, [self, sp](beast::error_code ec, std::size_t bytes) {
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
        stream_.socket().shutdown(tcp::socket::shutdown_send, ignored_ec);
        return;
    }
    request_ = {};
    // Keep buffered pipelined bytes for the next parser.
    do_read();
}

} // namespace clipstream::server
