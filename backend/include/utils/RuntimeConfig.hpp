#pragma once
#include "utils/Environment.hpp"
#include <nlohmann/json.hpp>
namespace clipstream::utils {
inline std::string legal_consent_version() {
    auto version = env("LEGAL_CONSENT_VERSION");
    return version.empty() ? "2026-11-14" : version;
}
inline nlohmann::json runtime_config() {
    auto document = [](const char *name, const char *fallback) {
        auto url = env(name);
        return url.empty() ? std::string(fallback) : url;
    };
    nlohmann::json documents = {{"privacy", document("LEGAL_PRIVACY_URL", "/legal/privacy")},
                                {"terms", document("LEGAL_TERMS_URL", "/legal/terms")},
                                {"consent", document("LEGAL_CONSENT_URL", "/legal/consent")},
                                {"cookies", document("LEGAL_COOKIES_URL", "/legal/cookies")},
                                {"offer", document("LEGAL_OFFER_URL", "/legal/offer")}};
    // Bundled document availability is not an operator's legal approval of their contents.
    return {{"legal",
             {{"version", legal_consent_version()},
              {"configured", true},
              {"operator_name", env("LEGAL_OPERATOR_NAME")},
              {"contact_email", env("LEGAL_CONTACT_EMAIL")},
              {"address", env("LEGAL_ADDRESS")},
              {"documents", documents}}},
            {"auth",
             {{"twitch_enabled", !env("TWITCH_CLIENT_ID").empty() && !env("TWITCH_CLIENT_SECRET").empty()},
              {"local_enabled", false}}}};
}
} // namespace clipstream::utils
