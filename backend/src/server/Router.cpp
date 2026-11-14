#include "server/Router.hpp"
#include "database/ConnectionPool.hpp"
#include "services/TwitchService.hpp"
#include "utils/Errors.hpp"
#include <algorithm>
#include <charconv>
#include <iostream>

namespace clipstream::server {

namespace {
std::string decode(const std::string &value, bool query) {
    std::string out;
    out.reserve(value.size());
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c == '%') {
            if (i + 2 >= value.size())
                throw std::invalid_argument("Malformed URL encoding");
            int hi = digit(value[i + 1]), lo = digit(value[i + 2]);
            if (hi < 0 || lo < 0)
                throw std::invalid_argument("Malformed URL encoding");
            c = static_cast<char>(hi * 16 + lo);
            i += 2;
        } else if (c == '+' && query)
            c = ' ';
        if (c == '\0' || static_cast<unsigned char>(c) < 32)
            throw std::invalid_argument("Invalid URL character");
        out += c;
    }
    json(out).dump(); // Reject malformed UTF-8 before SQL or response serialization.
    return out;
}
uint64_t unsigned_value(const std::string &value) {
    uint64_t result{};
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument("Invalid unsigned integer");
    return result;
}
} // namespace

void Router::get(const std::string &path, Handler handler) {
    add_route(http::verb::get, path, std::move(handler));
}
void Router::post(const std::string &path, Handler handler) {
    add_route(http::verb::post, path, std::move(handler));
}
void Router::put(const std::string &path, Handler handler) {
    add_route(http::verb::put, path, std::move(handler));
}
void Router::del(const std::string &path, Handler handler) {
    add_route(http::verb::delete_, path, std::move(handler));
}
void Router::use(Middleware mw) { middlewares_.push_back(std::move(mw)); }

void Router::add_route(http::verb method, const std::string &path, Handler handler) {
    auto [pattern, names] = compile_pattern(path);
    routes_.push_back({method, std::move(pattern), std::move(names), std::move(handler)});
}

std::pair<std::regex, std::vector<std::string>> Router::compile_pattern(const std::string &path) {
    std::vector<std::string> names;
    std::string regex_str;

    size_t i = 0;
    while (i < path.size()) {
        if (path[i] == ':') {
            ++i;
            std::string name;
            while (i < path.size() && path[i] != '/')
                name += path[i++];
            names.push_back(name);
            regex_str += "([^/]+)";
        } else {
            if (path[i] == '.')
                regex_str += "\\.";
            else
                regex_str += path[i];
            ++i;
        }
    }

    return {std::regex("^" + regex_str + "$"), names};
}

std::unordered_map<std::string, std::string> Router::parse_query(const std::string &qs) {
    std::unordered_map<std::string, std::string> result;
    std::istringstream stream(qs);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        auto eq = pair.find('=');
        if (eq != std::string::npos)
            result[decode(pair.substr(0, eq), true)] = decode(pair.substr(eq + 1), true);
    }
    return result;
}

Response Router::dispatch(http::verb method, const std::string &target, const std::string &body_str,
                          const std::string &auth_header, const std::string &client_address) {
    try {
        if (target.size() > 16384 || body_str.size() > 65536)
            return {413, {{"error", "Request too large"}}};
        std::string path = target;
        std::string query_str;
        auto qpos = target.find('?');
        if (qpos != std::string::npos) {
            path = target.substr(0, qpos);
            query_str = target.substr(qpos + 1);
        }

        for (auto &route : routes_) {
            if (route.method != method)
                continue;

            std::smatch match;
            if (!std::regex_match(path, match, route.pattern))
                continue;

            Request req;
            req.method = method;
            req.path = path;
            req.query = parse_query(query_str);
            req.auth_token = auth_header;
            req.client_address = client_address;

            for (size_t i = 0; i < route.param_names.size(); ++i) {
                auto value = decode(match[i + 1].str(), false);
                if (value.find('/') != std::string::npos)
                    throw std::invalid_argument("Invalid path segment");
                const auto &name = route.param_names[i];
                if (name == "id" || name == "clip_id" || name == "userId") {
                    if (unsigned_value(value) == 0)
                        throw std::invalid_argument("ID must be positive");
                }
                req.params[name] = std::move(value);
            }
            for (auto &[key, value] : req.query) {
                if (key == "limit")
                    value = std::to_string(std::clamp<uint64_t>(unsigned_value(value), 1, 100));
                else if (key == "offset")
                    value = std::to_string(std::min<uint64_t>(unsigned_value(value), 10000));
                else if (key == "game_id" || key == "parent_id" || key == "hours" || key == "mix")
                    unsigned_value(value);
                if (value.size() > 4096)
                    throw std::invalid_argument("Query value too long");
            }

            if (!body_str.empty()) {
                try {
                    req.body = json::parse(body_str);
                } catch (...) {
                    return {400, {{"error", "Invalid JSON"}}};
                }
            }

            Response res;
            for (auto &mw : middlewares_) {
                if (!mw(req, res))
                    return res;
            }

            return route.handler(req);
        }

        return {404, {{"error", "Not found"}}};
    } catch (const utils::PermissionDenied &e) {
        return {403, {{"error", e.what()}}};
    } catch (const utils::NotFound &e) {
        return {404, {{"error", e.what()}}};
    } catch (const services::TwitchError &e) {
        return {static_cast<unsigned>(e.status == 401 ? 502 : e.status),
                {{"error", e.what()}, {"retry_after", e.retry_after}}};
    } catch (const database::Error &e) {
        std::cerr << "[DB] code=" << e.code << "\n";
        return {503, {{"error", "Database temporarily unavailable"}}};
    } catch (const json::exception &) {
        return {400, {{"error", "Invalid JSON field type or value"}}};
    } catch (const std::invalid_argument &e) {
        return {400, {{"error", e.what()}}};
    } catch (const std::out_of_range &) {
        return {400, {{"error", "Numeric value out of range"}}};
    } catch (const std::exception &e) {
        std::cerr << "[Router] " << e.what() << "\n";
        return {503, {{"error", "Service temporarily unavailable"}}};
    }
}

} // namespace clipstream::server
