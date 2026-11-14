#pragma once

#include "services/AuthService.hpp"
#include "services/CacheService.hpp"
#include "services/TwitchService.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

namespace clipstream::services {

class EventSubService {
  public:
    EventSubService(TwitchService &twitch, CacheService &cache, AuthService &auth,
                    std::vector<std::string> broadcaster_ids,
                    std::function<std::string()> token_provider = {});
    ~EventSubService();

    void start();
    void stop();

    nlohmann::json status() const;
    nlohmann::json stream_state(const std::string &broadcaster_id) const;
    nlohmann::json latest_clip(const std::string &broadcaster_id) const;

  private:
    void run();
    void publish_status() const;
    void subscribe_all(const std::string &session_id, std::stop_token stop);
    void subscribe_type(const std::string &type, const std::string &version,
                        const std::string &broadcaster_id, const std::string &session_id);
    void subscribe_type_with_condition(const std::string &type, const std::string &version,
                                       const nlohmann::json &condition, const std::string &session_id);
    void handle_message(const nlohmann::json &msg);
    static std::string now_iso();

    TwitchService &twitch_;
    CacheService &cache_;
    AuthService &auth_;
    std::vector<std::string> broadcaster_ids_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    mutable std::mutex state_mx_;
    bool connected_{false};
    std::string session_id_;
    std::string last_message_at_;
    std::string last_error_;
    std::string session_token_;
    std::function<std::string()> token_provider_;
    nlohmann::json subscriptions_ = nlohmann::json::array();
    bool enabled_{false};
    bool subscribed_{false};
    mutable std::mutex cancel_mx_;
    std::function<void()> cancel_;
    std::condition_variable wake_;
    std::mutex wake_mx_;
};

} // namespace clipstream::services
