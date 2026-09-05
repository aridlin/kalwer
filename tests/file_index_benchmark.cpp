#include "../file_index.hpp"
#include <iostream>
int main(int argc, char** argv) {
    using namespace kalwer_files;
    if (argc != 2) { std::cerr << "Usage: file-index-benchmark INDEX_DIRECTORY\n"; return 1; }
    Index index(std::chrono::seconds(60), from_utf8(argv[1]), false);
    Database db(from_utf8(argv[1]) / "files-v2.sqlite");
    Statement count(db, "SELECT count(*) FROM files");
    sqlite3_step(count.handle);
    std::cout << "Indexed paths: " << sqlite3_column_int64(count.handle, 0) << '\n';
    const std::vector<std::string> queries = {"file_index.hpp", "report", "kalwer", "readme", "png", "zzzznotfound987", "dev kalwer", "ma"};
    for (const auto& query : queries) {
        std::vector<double> timings;
        size_t results = 0;
        for (int pass = 0; pass < 10; ++pass) {
            auto start = std::chrono::steady_clock::now();
            auto id = index.request(query);
            while (true) {
                auto reply = index.poll();
                if (reply.request == id) { results = reply.entries.size(); break; }
                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) return 2;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            timings.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        std::sort(timings.begin(), timings.end());
        std::cout << query << ": median=" << timings[5] << " ms max=" << timings.back() << " ms results=" << results << '\n';
    }
}
