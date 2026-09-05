#pragma once
#include <functional>
#include <algorithm>
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

#include <filesystem>
#include <fstream>
namespace kalwer {
// Count launcher openings, not frames or seconds; keep the count across restarts.
class UpdateBanner {
    std::filesystem::path file_;
    std::string version_;
    int remaining_ = 0;
    bool visible_ = false;
    void save() const {
        std::error_code error;
        std::filesystem::create_directories(file_.parent_path(), error);
        std::ofstream out(file_); out << version_ << '\n' << remaining_ << '\n';
    }
public:
    void load(std::filesystem::path file, std::string version, bool updated) {
        file_ = std::move(file); version_ = std::move(version);
        std::string previous; int remaining = 0;
        std::ifstream in(file_);
        const bool saved = static_cast<bool>(in >> previous >> remaining);
        if (saved && previous == version_) remaining_ = std::clamp(remaining, 0, 3);
        else if (updated || (saved && previous != version_)) remaining_ = 3;
        else remaining_ = 0;
        save();
    }
    void opened() { visible_ = remaining_ > 0; if (visible_) { --remaining_; save(); } }
    bool visible() const { return visible_; }
    int offset() const { return visible_ ? 32 : 0; }
};
}
