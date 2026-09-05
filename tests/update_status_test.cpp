#include "../update_status.hpp"
#include <cassert>
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
    std::cout << "Update success and early-return failure notification checks passed.\n";
}
