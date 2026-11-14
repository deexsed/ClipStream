#include "server/BlockingExecutor.hpp"
#include "server/Router.hpp"
#include "utils/Crypto.hpp"
#include "utils/Network.hpp"
#include "utils/RuntimeConfig.hpp"
#include <cstdlib>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
using namespace clipstream;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void set_environment(const std::string &name, const std::optional<std::string> &value) {
#ifdef _WIN32
    _putenv_s(name.c_str(), value ? value->c_str() : "");
#else
    if (value)
        setenv(name.c_str(), value->c_str(), 1);
    else
        unsetenv(name.c_str());
#endif
}
struct ScopedEnvironment {
    std::vector<std::pair<std::string, std::optional<std::string>>> saved;
    explicit ScopedEnvironment(std::initializer_list<const char *> names) {
        for (const auto name : names) {
            for (const auto &key : {std::string(name), std::string(name) + "_FILE"}) {
                const auto value = std::getenv(key.c_str());
                saved.emplace_back(key, value ? std::make_optional(std::string(value)) : std::nullopt);
                set_environment(key, std::nullopt);
            }
        }
    }
    ~ScopedEnvironment() {
        for (const auto &[key, value] : saved)
            set_environment(key, value);
    }
};
int main() {
    try {
        {
            ScopedEnvironment isolated{"LEGAL_PRIVACY_URL",   "LEGAL_TERMS_URL",     "LEGAL_CONSENT_URL",
                                       "LEGAL_COOKIES_URL",   "LEGAL_OFFER_URL",     "LEGAL_CONSENT_VERSION",
                                       "LEGAL_OPERATOR_NAME", "LEGAL_CONTACT_EMAIL", "LEGAL_ADDRESS",
                                       "TWITCH_CLIENT_ID",    "TWITCH_CLIENT_SECRET"};
            const auto defaults = utils::runtime_config();
            require(defaults["legal"]["configured"] == true, "Bundled legal documents unavailable");
            require(defaults["legal"]["version"] == "2026-11-14", "Wrong default consent version");
            require(defaults["auth"]["twitch_enabled"] == false, "Missing credentials enabled OAuth");
            for (const auto key : {"privacy", "terms", "consent", "cookies", "offer"})
                require(defaults["legal"]["documents"][key] == "/legal/" + std::string(key),
                        "Bundled legal URL missing");
            set_environment("TWITCH_CLIENT_ID", "fixture-client");
            set_environment("TWITCH_CLIENT_SECRET", "fixture-secret");
            require(utils::runtime_config()["auth"]["twitch_enabled"] == true,
                    "Blank operator identity blocked configured OAuth");
            require(utils::runtime_config()["auth"]["local_enabled"] == false, "Local auth enabled");
            set_environment("LEGAL_PRIVACY_URL", "https://operator.example/privacy");
            set_environment("LEGAL_TERMS_URL", "");
            set_environment("LEGAL_CONSENT_VERSION", "operator-v2");
            const auto custom = utils::runtime_config();
            require(custom["legal"]["documents"]["privacy"] == "https://operator.example/privacy",
                    "External document override ignored");
            require(custom["legal"]["documents"]["terms"] == "/legal/terms",
                    "Empty env removed bundled document");
            require(custom["legal"]["version"] == utils::legal_consent_version() &&
                        utils::legal_consent_version() == "operator-v2",
                    "Auth and config consent versions diverged");
        }
        const unsigned char bytes[] = {0, 128, 255};
        auto encoded = utils::hex(bytes, 3);
        require(encoded == "0080ff", "Hex encoding");
        unsigned char decoded_bytes[3];
        require(utils::unhex(encoded, decoded_bytes, 3) && decoded_bytes[1] == 128, "Hex decoding");
        require(!utils::unhex("malformed", decoded_bytes, 3), "Invalid hex accepted");
        require(utils::normalize_ip("::ffff:192.0.2.7") == "192.0.2.7", "IP normalization");
        require(utils::normalize_ip("2001:db8:0:0::1") == "2001:db8::1", "IPv6 normalization");
        server::Router router;
        router.get("/users/:username", [](const server::Request &r) -> server::Response {
            return {200,
                    {{"name", r.params.at("username")},
                     {"query", r.query.at("q")},
                     {"limit", r.query.at("limit")}}};
        });
        auto decoded = router.dispatch(server::http::verb::get,
                                       "/users/%D0%98%D0%B2%D0%B0%D0%BD?q=Dota+2%26ru&limit=900", "", "");
        require(decoded.status == 200 && decoded.body["name"] == "Иван" &&
                    decoded.body["query"] == "Dota 2&ru" && decoded.body["limit"] == "100",
                "URL decoding or page cap");
        require(router.dispatch(server::http::verb::get, "/users/a?q=%ZZ&limit=1", "", "").status == 400,
                "Malformed URL accepted");
        require(router.dispatch(server::http::verb::get, "/users/a?q=x&limit=-1", "", "").status == 400,
                "Negative limit accepted");
        require(router.dispatch(server::http::verb::get, "/users/a?q=x&limit=1junk", "", "").status == 400,
                "Partial integer accepted");
        router.get("/clips/:id", [](const server::Request &) -> server::Response { return {200, {}}; });
        require(router.dispatch(server::http::verb::get, "/clips/3garbage", "", "").status == 400,
                "Invalid numeric ID accepted");
        server::BlockingExecutor executor(1, 1);
        std::promise<void> entered, release;
        auto ready = entered.get_future();
        auto gate = release.get_future().share();
        require(executor.post([&] {
            entered.set_value();
            gate.wait();
        }),
                "Initial executor post failed");
        ready.wait();
        require(!executor.post([] {}), "Executor queue is unbounded");
        release.set_value();
        executor.join();
        std::cout << "Core contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
