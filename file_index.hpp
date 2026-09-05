#pragma once
#include "vendor/sqlite/sqlite3.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace kalwer_files {
namespace fs = std::filesystem;
inline std::string text(const fs::path& p) {
    const auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
inline fs::path from_utf8(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
inline std::string fold(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
struct Entry { std::string path, name; };
struct Reply { unsigned long long request = 0, revision = 0; std::vector<Entry> entries; std::string status; };
struct Database {
    sqlite3* handle = nullptr;
    explicit Database(const fs::path& path) {
        if (sqlite3_open(text(path).c_str(), &handle) != SQLITE_OK) {
            const std::string error = handle ? sqlite3_errmsg(handle) : "Cannot open index";
            sqlite3_close(handle); handle = nullptr; throw std::runtime_error(error);
        }
        sqlite3_busy_timeout(handle, 3000);
    }
    ~Database() { sqlite3_close(handle); }
    void exec(const char* sql) {
        if (sqlite3_exec(handle, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(handle));
    }
};
struct Statement {
    sqlite3_stmt* handle = nullptr;
    Statement(Database& db, const std::string& sql) {
        if (sqlite3_prepare_v2(db.handle, sql.c_str(), -1, &handle, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db.handle));
    }
    ~Statement() { sqlite3_finalize(handle); }
    void bind(int n, const std::string& value) {
        sqlite3_bind_text(handle, n, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }
};
class Index {
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false}, refresh_requested_{false};
    std::thread scan_, search_;
    fs::path directory_;
    bool update_ = true;
    bool started_ = false, ready_ = false, scanning_ = true;
    std::chrono::milliseconds refresh_;
    std::atomic<unsigned long long> request_{0};
    unsigned long long generation_ = 0;
    std::string query_, error_;
    Reply reply_;
    void changed(bool scanning) {
        std::lock_guard lock(mutex_);
        scanning_ = scanning; ++generation_; cv_.notify_all();
    }
    void scan() {
        Database db(directory_ / "files-v2.sqlite");
        sqlite3_progress_handler(db.handle, 1000, [](void* value) -> int {
            return static_cast<Index*>(value)->stop_.load();
        }, this);
        db.exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA cache_size=-8192;"
                "CREATE TABLE IF NOT EXISTS files(id INTEGER PRIMARY KEY,path TEXT UNIQUE,name TEXT COLLATE NOCASE,folded TEXT,seen INTEGER);"
                "CREATE INDEX IF NOT EXISTS file_names ON files(name);"
                "CREATE VIRTUAL TABLE IF NOT EXISTS paths USING fts5(folded,content=files,content_rowid=id,tokenize='trigram case_sensitive 1',detail=none);"
                "CREATE TRIGGER IF NOT EXISTS files_ai AFTER INSERT ON files BEGIN INSERT INTO paths(rowid,folded) VALUES(new.id,new.folded); END;"
                "CREATE TRIGGER IF NOT EXISTS files_ad AFTER DELETE ON files BEGIN INSERT INTO paths(paths,rowid,folded) VALUES('delete',old.id,old.folded); END;");
        { std::lock_guard lock(mutex_); ready_ = true; cv_.notify_all(); }
        while (!stop_) {
            std::vector<fs::path> roots;
            std::ifstream config(directory_ / "file-roots.txt");
            std::string line;
            while (std::getline(config, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty() && line.front() != '#') {
                    auto p = from_utf8(line);
                    if (p.is_absolute()) roots.push_back(p.lexically_normal());
                }
            }
            if (!config.is_open()) {
#ifdef _WIN32
                if (const auto* home = _wgetenv(L"USERPROFILE")) roots.emplace_back(home);
#else
                if (const auto* home = std::getenv("HOME")) roots.emplace_back(home);
#endif
            }
            sqlite3_int64 generation = 1;
            { Statement last(db, "SELECT coalesce(max(seen),0)+1 FROM files");
              if (sqlite3_step(last.handle) == SQLITE_ROW) generation = sqlite3_column_int64(last.handle, 0); }
            Statement insert(db, "INSERT INTO files(path,name,folded,seen) VALUES(?1,?2,?3,?4) ON CONFLICT(path) DO UPDATE SET seen=excluded.seen");
            db.exec("BEGIN");
            unsigned batch = 0;
            auto published = std::chrono::steady_clock::now();
            for (const auto& root : roots) {
                std::error_code ec;
                fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
                while (!stop_ && it != end) {
                    const auto p = it->path();
                    const auto name = text(p.filename());
                    const bool dir = it->is_directory(ec); ec.clear();
                    const bool excluded = dir && (name == ".git" || name == ".cache" || name == "node_modules" ||
                        name == "$RECYCLE.BIN" || name == "System Volume Information" || name == "Cache" || name == "Caches" || p == directory_);
                    if (excluded) it.disable_recursion_pending();
                    else {
                        const auto path = text(p);
                        sqlite3_reset(insert.handle);
                        insert.bind(1, path); insert.bind(2, name); insert.bind(3, fold(path));
                        sqlite3_bind_int64(insert.handle, 4, generation);
                        if (sqlite3_step(insert.handle) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db.handle));
                        if (++batch % 1000 == 0) {
                            db.exec("COMMIT; BEGIN");
                            if (std::chrono::steady_clock::now() - published > std::chrono::seconds(2)) {
                                changed(true); published = std::chrono::steady_clock::now();
                            }
                        }
                    }
                    it.increment(ec); if (ec) ec.clear();
                }
            }
            if (!stop_) {
                Statement prune(db, "DELETE FROM files WHERE seen<>?1");
                sqlite3_bind_int64(prune.handle, 1, generation);
                if (sqlite3_step(prune.handle) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db.handle));
            }
            db.exec("COMMIT");
            changed(false);
            if (generation == 1 && !stop_) db.exec("INSERT INTO paths(paths) VALUES('optimize')");
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock, refresh_, [&] { return stop_.load() || refresh_requested_.load(); });
            refresh_requested_ = false;
        }
    }
    static std::string glob(const std::string& value) {
        std::string out;
        for (const char c : value) {
            if (c == '*') out += "[*]";
            else if (c == '?') out += "[?]";
            else if (c == '[') out += "[[]";
            else out += c;
        }
        return out;
    }
    struct Progress {
        Index* owner;
        unsigned long long request;
        std::chrono::steady_clock::time_point deadline;
    };
    static int progress(void* value) {
        const auto& p = *static_cast<Progress*>(value);
        return p.owner->stop_ || p.request != p.owner->request_ || std::chrono::steady_clock::now() > p.deadline;
    }
    void search() {
        { std::unique_lock lock(mutex_); cv_.wait(lock, [&] { return ready_ || stop_ || !error_.empty(); });
          if (!ready_ || stop_) return; }
        Database db(directory_ / "files-v2.sqlite");
        db.exec("PRAGMA cache_size=-8192; PRAGMA query_only=ON");
        unsigned long long previous = 0, generation = ~0ULL;
        while (!stop_) {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return stop_ || previous != request_ || generation != generation_; });
            if (stop_) break;
            previous = request_; generation = generation_;
            const auto query = fold(query_);
            const bool scanning = scanning_;
            const auto error = error_;
            lock.unlock();
            Reply result; result.request = previous;
            Progress budget{this, previous, std::chrono::steady_clock::now() + std::chrono::milliseconds(200)};
            sqlite3_progress_handler(db.handle, 1000, progress, &budget);
            auto collect = [&](const std::string& sql, const std::vector<std::string>& args) {
                Statement statement(db, sql);
                for (size_t i = 0; i < args.size(); ++i) statement.bind(static_cast<int>(i + 1), args[i]);
                int rc;
                while ((rc = sqlite3_step(statement.handle)) == SQLITE_ROW) {
                    result.entries.push_back({reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 0)),
                                              reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 1))});
                }
                if (rc != SQLITE_DONE && rc != SQLITE_INTERRUPT) throw std::runtime_error(sqlite3_errmsg(db.handle));
            };
            // The name B-tree also handles one- and two-character queries quickly.
            std::string prefix;
            for (const auto c : query) { if (c == '%' || c == '_' || c == '\\') prefix += '\\'; prefix += c; }
            collect("SELECT path,name FROM files WHERE name=?1 COLLATE NOCASE LIMIT 512", {query});
            collect("SELECT path,name FROM files WHERE name LIKE ?1 ESCAPE '\\' ORDER BY name LIMIT 512", {prefix + "%"});
            std::istringstream words(query); std::string word;
            std::vector<std::string> patterns;
            bool indexed = false;
            while (words >> word) {
                size_t characters = 0;
                for (unsigned char c : word) if ((c & 0xc0) != 0x80) ++characters;
                indexed |= characters >= 3;
                patterns.push_back("*" + glob(word) + "*");
            }
            if (indexed && result.entries.size() < 512) {
                std::string sql = "SELECT files.path,files.name FROM paths JOIN files ON files.id=paths.rowid WHERE ";
                for (size_t i = 0; i < patterns.size(); ++i) {
                    if (i) sql += " AND ";
                    sql += "paths.folded GLOB ?" + std::to_string(i + 1);
                }
                collect(sql + " LIMIT 1024", patterns);
            }
            sqlite3_progress_handler(db.handle, 0, nullptr, nullptr);
            auto score = [&](const Entry& e) {
                const auto name = fold(e.name);
                return name == query ? 0 : name.starts_with(query) ? 1 : name.find(query) != std::string::npos ? 2 : 3;
            };
            std::sort(result.entries.begin(), result.entries.end(), [&](const Entry& a, const Entry& b) {
                const int x = score(a), y = score(b); return x != y ? x < y : a.path < b.path;
            });
            result.entries.erase(std::unique(result.entries.begin(), result.entries.end(), [](const Entry& a, const Entry& b) { return a.path == b.path; }), result.entries.end());
            if (result.entries.size() > 512) result.entries.resize(512);
            result.status = !error.empty() ? error : std::chrono::steady_clock::now() > budget.deadline ? "Narrow the query for more precise results" : scanning ? "Indexing files…" : !indexed && !query.empty() ? "Use 3+ characters for substring search" : "No matching files";
            lock.lock();
            if (previous == request_) { result.revision = reply_.revision + 1; reply_ = std::move(result); }
        }
    }
    template<class Work> void guarded(Work work) {
        try { work(); }
        catch (const std::exception& e) {
            std::lock_guard lock(mutex_);
            error_ = std::string("File index: ") + e.what();
            reply_.request = request_; reply_.status = error_; ++reply_.revision;
            ++generation_; cv_.notify_all();
        }
    }
public:
    explicit Index(std::chrono::milliseconds refresh = std::chrono::seconds(60), fs::path directory = {}, bool update = true)
        : directory_(std::move(directory)), update_(update), refresh_(refresh) {}
    ~Index() {
        stop_ = true; cv_.notify_all();
        if (scan_.joinable()) scan_.join();
        if (search_.joinable()) search_.join();
    }
    unsigned long long request(std::string query) {
        std::lock_guard lock(mutex_);
        if (!started_) {
            if (directory_.empty()) {
#ifdef _WIN32
            const auto* base = _wgetenv(L"LOCALAPPDATA");
            directory_ = (base ? fs::path(base) : fs::temp_directory_path()) / "Kalwer";
#else
            const auto* base = std::getenv("XDG_CACHE_HOME");
            const auto* home = std::getenv("HOME");
            directory_ = (base && *base ? fs::path(base) : fs::path(home ? home : "/tmp") / ".cache") / "kalwer";
#endif
            }
            std::error_code ec; fs::create_directories(directory_, ec);
            started_ = true;
            if (update_) scan_ = std::thread([this] { guarded([this] { scan(); }); });
            else { ready_ = true; scanning_ = false; }
            search_ = std::thread([this] { guarded([this] { search(); }); });
        }
        query_ = std::move(query); ++request_; cv_.notify_all(); return request_;
    }
    void refresh() { request(""); refresh_requested_ = true; cv_.notify_all(); }
    Reply poll(unsigned long long after = 0) {
        std::lock_guard lock(mutex_); return reply_.revision == after ? Reply{} : reply_;
    }
};
}
