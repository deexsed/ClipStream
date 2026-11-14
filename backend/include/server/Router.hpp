#pragma once

#include <boost/beast/http.hpp>
#include <functional>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace clipstream::server {

namespace http = boost::beast::http;
using json = nlohmann::json;

struct Request {
    http::verb method;
    std::string path;
    json body;
    std::unordered_map<std::string, std::string> params;
    std::unordered_map<std::string, std::string> query;
    std::string auth_token;
    uint64_t user_id{};
    std::string client_address;
};

struct Response {
    unsigned status{200};
    json body;
};

using Handler = std::function<Response(const Request &)>;
using Middleware = std::function<bool(Request &, Response &)>;

struct Route {
    http::verb method;
    std::regex pattern;
    std::vector<std::string> param_names;
    Handler handler;
};

class Router {
  public:
    void get(const std::string &path, Handler handler);
    void post(const std::string &path, Handler handler);
    void put(const std::string &path, Handler handler);
    void del(const std::string &path, Handler handler);

    void use(Middleware mw);

    Response dispatch(http::verb method, const std::string &target, const std::string &body_str,
                      const std::string &auth_header, const std::string &client_address = "unknown");

  private:
    void add_route(http::verb method, const std::string &path, Handler handler);
    static std::pair<std::regex, std::vector<std::string>> compile_pattern(const std::string &path);
    static std::unordered_map<std::string, std::string> parse_query(const std::string &query_str);

    std::vector<Route> routes_;
    std::vector<Middleware> middlewares_;
};

} // namespace clipstream::server
