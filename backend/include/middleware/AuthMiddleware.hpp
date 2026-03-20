#pragma once

#include "server/Router.hpp"
#include "services/AuthService.hpp"

namespace clipstream::middleware {

// Middleware, извлекающий user_id из JWT-токена (если присутствует)
inline server::Middleware make_auth_middleware(services::AuthService& auth) {
    return [&auth](server::Request& req, server::Response& /*res*/) -> bool {
        if (req.auth_token.empty()) return true;

        std::string token = req.auth_token;
        if (token.substr(0, 7) == "Bearer ")
            token = token.substr(7);

        auto user_id = auth.verify_token(token);
        if (user_id) req.user_id = *user_id;

        return true;
    };
}

// Middleware для маршрутов, требующих авторизации
inline server::Middleware make_require_auth() {
    return [](server::Request& req, server::Response& res) -> bool {
        if (req.user_id == 0) {
            res.status = 401;
            res.body = {{"error", "Unauthorized"}};
            return false;
        }
        return true;
    };
}

} // namespace clipstream::middleware
