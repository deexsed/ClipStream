#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>
#include "services/TwitchService.hpp"
#include "services/CacheService.hpp"
#include "services/AuthService.hpp"

namespace clipstream::services {

class EventSubService {
public:
    EventSubService(TwitchService& twitch, CacheService& cache, AuthService& auth, std::vector<std::string> broadcaster_ids);
    ~EventSubService();

    void start();
    void stop();

    nlohmann::json status() const;
    nlohmann::json stream_state(const std::string& broadcaster_id) const;
    nlohmann::json latest_clip(const std::string& broadcaster_id) const;

private:
    void run();
    void subscribe_all(const std::string& session_id);
    void subscribe_type(const std::string& type, const std::string& version,
                        const std::string& broadcaster_id, const std::string& session_id);
    void subscribe_type_with_condition(const std::string& type, const std::string& version,
                                       const nlohmann::json& condition, const std::string& session_id);
    void handle_message(const nlohmann::json& msg);
    static std::string now_iso();

    TwitchService& twitch_;
    CacheService& cache_;
    AuthService& auth_;
    std::vector<std::string> broadcaster_ids_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    mutable std::mutex state_mx_;
    bool connected_{false};
    std::string session_id_;
    std::string last_message_at_;
    std::string last_error_;
};

} // namespace clipstream::services

