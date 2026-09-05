#include "../file_index.hpp"
#include <cassert>
#include <iostream>
using namespace kalwer_files;
Reply wait_for(Index& index, unsigned long long request) {
    for (int n = 0; n < 500; ++n) {
        auto reply = index.poll();
        if (reply.request == request && reply.status != "Indexing files…") return reply;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("index timed out");
}
int main() {
    const auto root = fs::temp_directory_path() / ("kalwer-index-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto cache = root / "cache";
    const auto files = root / "files";
    fs::create_directories(cache / "kalwer");
    fs::create_directories(files / "sub");
    fs::create_directories(files / "node_modules");
    std::ofstream(files / "Budget.TXT") << "data";
    std::ofstream(files / "sub" / "budget report.txt") << "data";
    std::ofstream(files / "node_modules" / "budget hidden.txt") << "data";
    std::ofstream(files / from_utf8("角色 notes.txt")) << "data";
    std::ofstream(files / "line\nbreak.txt") << "data";
    std::ofstream(cache / "kalwer" / "file-roots.txt") << text(files) << '\n' << text(files / "sub") << '\n';
    setenv("XDG_CACHE_HOME", text(cache).c_str(), 1);
    {
        Index index(std::chrono::milliseconds(50));
        auto reply = wait_for(index, index.request("BUDGET"));
        assert(reply.entries.size() == 2);
        reply = wait_for(index, index.request("budget.txt"));
        assert(reply.entries.size() == 1 && reply.entries[0].name == "Budget.TXT");
        reply = wait_for(index, index.request("sub report"));
        assert(reply.entries.size() == 1);
        reply = wait_for(index, index.request("角色"));
        assert(reply.entries.size() == 1);
        index.request("budget");
        reply = wait_for(index, index.request("missing-file"));
        assert(reply.entries.empty());
        fs::remove(files / "Budget.TXT");
        std::ofstream(files / "newly-created.txt") << "new";
        const auto fresh = index.request("newly-created");
        bool updated = false;
        for (int n = 0; n < 500; ++n) {
            reply = index.poll();
            if (reply.request == fresh && reply.entries.size() == 1) { updated = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(updated);
        reply = wait_for(index, index.request("Budget.TXT"));
        assert(reply.entries.empty());
    }
    {
        Index index;
        auto reply = wait_for(index, index.request("break.txt"));
        assert(reply.entries.size() == 1 && reply.entries[0].name == "line\nbreak.txt");
    }
    fs::remove_all(root);
    std::cout << "File index tests passed: ranking, terms, exclusions, overlapping roots, Unicode, latest query, cache restart, creation and deletion refresh.\n";
}
