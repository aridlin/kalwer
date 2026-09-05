#include "../update_status.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>
int main() {
    std::vector<std::string> notices;
    auto notify = [&](const std::string& message) { notices.push_back(message); };
    { kalwer::UpdateAttempt failed(notify); }
    assert(notices.size() == 1 && notices[0].find("Update failed") == 0);
    notices.clear();
    { kalwer::UpdateAttempt success(notify); success.complete("Ready; restart to install"); }
    assert(notices.size() == 1 && notices[0] == "Ready; restart to install");
    kalwer::UpdateStatus status;
    status.set(notices[0]); assert(status.get() == notices[0]);
    const auto file = std::filesystem::temp_directory_path() / ("kalwer-banner-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    kalwer::UpdateBanner first;
    first.load(file, "1.0", false); first.opened(); assert(!first.visible());
    first.load(file, "1.1", true); first.opened(); assert(first.visible());
    kalwer::UpdateBanner restarted;
    restarted.load(file, "1.1", true);
    restarted.opened(); assert(restarted.visible());
    restarted.opened(); assert(restarted.visible());
    restarted.opened(); assert(!restarted.visible());
    restarted.load(file, "1.1", true); restarted.opened(); assert(!restarted.visible());
    restarted.load(file, "1.2", false); restarted.opened(); assert(restarted.visible());
    std::filesystem::remove(file);
    std::cout << "Update success and early-return failure notification and three-opening persistent banner checks passed.\n";
}
