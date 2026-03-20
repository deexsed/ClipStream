#include "server/Router.hpp"
#include <iostream>

namespace clipstream::server {

void Router::get(const std::string& path, Handler handler)  { add_route(http::verb::get,    path, std::move(handler)); }
void Router::post(const std::string& path, Handler handler) { add_route(http::verb::post,   path, std::move(handler)); }
void Router::put(const std::string& path, Handler handler)  { add_route(http::verb::put,    path, std::move(handler)); }
void Router::del(const std::string& path, Handler handler)  { add_route(http::verb::delete_, path, std::move(handler)); }
void Router::use(Middleware mw) { middlewares_.push_back(std::move(mw)); }

void Router::add_route(http::verb method, const std::string& path, Handler handler) {
    auto [pattern, names] = compile_pattern(path);
    routes_.push_back({method, std::move(pattern), std::move(names), std::move(handler)});
}

std::pair<std::regex, std::vector<std::string>> Router::compile_pattern(const std::string& path) {
    std::vector<std::string> names;
    std::string regex_str;

    size_t i = 0;
    while (i < path.size()) {
        if (path[i] == ':') {
            ++i;
            std::string name;
            while (i < path.size() && path[i] != '/') name += path[i++];
            names.push_back(name);
            regex_str += "([^/]+)";
        } else {
            if (path[i] == '.') regex_str += "\\.";
            else regex_str += path[i];
            ++i;
        }
    }

    return {std::regex("^" + regex_str + "$"), names};
}

std::unordered_map<std::string, std::string> Router::parse_query(const std::string& qs) {
    std::unordered_map<std::string, std::string> result;
    std::istringstream stream(qs);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        auto eq = pair.find('=');
        if (eq != std::string::npos)
            result[pair.substr(0, eq)] = pair.substr(eq + 1);
    }
    return result;
}

Response Router::dispatch(http::verb method, const std::string& target,
                          const std::string& body_str, const std::string& auth_header) {
    std::string path = target;
    std::string query_str;
    auto qpos = target.find('?');
    if (qpos != std::string::npos) {
        path = target.substr(0, qpos);
        query_str = target.substr(qpos + 1);
    }

    for (auto& route : routes_) {
        if (route.method != method) continue;

        std::smatch match;
        if (!std::regex_match(path, match, route.pattern)) continue;

        Request req;
        req.method = method;
        req.path = path;
        req.query = parse_query(query_str);
        req.auth_token = auth_header;

        for (size_t i = 0; i < route.param_names.size(); ++i)
            req.params[route.param_names[i]] = match[i + 1].str();

        if (!body_str.empty()) {
            try { req.body = json::parse(body_str); }
            catch (...) { return {400, {{"error", "Invalid JSON"}}}; }
        }

        Response res;
        for (auto& mw : middlewares_) {
            if (!mw(req, res)) return res;
        }

        try {
            return route.handler(req);
        } catch (const std::exception& e) {
            std::cerr << "[Router] Handler error: " << e.what() << "\n";
            return {500, {{"error", "Internal server error"}}};
        }
    }

    return {404, {{"error", "Not found"}}};
}

} // namespace clipstream::server
