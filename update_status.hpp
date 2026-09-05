#pragma once
#include <functional>
#include <mutex>
#include <string>
namespace kalwer {
class UpdateStatus {
    mutable std::mutex mutex_;
    std::string message_;
public:
    void set(std::string message) { std::lock_guard lock(mutex_); message_ = std::move(message); }
    std::string get() const { std::lock_guard lock(mutex_); return message_; }
};
// Once download starts, every exit must produce either a success or a failure notice.
class UpdateAttempt {
    std::function<void(const std::string&)> notify_;
    bool finished_ = false;
public:
    explicit UpdateAttempt(std::function<void(const std::string&)> notify) : notify_(std::move(notify)) {}
    ~UpdateAttempt() {
        if (!finished_) notify_("Update failed: download, verification or installation did not complete. Your current version is still available. See /updates.");
    }
    void complete(const std::string& message) { finished_ = true; notify_(message); }
};
}
