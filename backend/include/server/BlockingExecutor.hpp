#pragma once
#include <atomic>
#include <boost/asio.hpp>
#include <functional>
namespace clipstream::server {
// The bound includes running handlers and queued work; callers receive 503 on saturation.
class BlockingExecutor {
  public:
    explicit BlockingExecutor(size_t threads, size_t capacity = 64) : pool_(threads), capacity_(capacity) {}
    bool post(std::function<void()> operation) {
        size_t current = pending_.load();
        do {
            if (stopping_ || current >= capacity_)
                return false;
        } while (!pending_.compare_exchange_weak(current, current + 1));
        boost::asio::post(pool_, [this, operation = std::move(operation)] {
            struct Done {
                std::atomic<size_t> &pending;
                ~Done() { --pending; }
            } done{pending_};
            try {
                operation();
            } catch (...) {
            }
        });
        return true;
    }
    void stop() {
        stopping_ = true;
        pool_.stop();
    }
    void join() {
        stopping_ = true;
        pool_.join();
    }

  private:
    boost::asio::thread_pool pool_;
    const size_t capacity_;
    std::atomic<size_t> pending_{0};
    std::atomic<bool> stopping_{false};
};
} // namespace clipstream::server
