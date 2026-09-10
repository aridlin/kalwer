#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <cstdint>

namespace kalwer {
// One running job and one replaceable pending job; results are versioned.
// The worker owns its inputs and never touches UI objects.
template<class Input, class Output> class LatestWorker {
public:
    struct Reply { std::uint64_t generation; Output value; };
    explicit LatestWorker(std::function<Output(Input)> work) : work_(std::move(work)), thread_([this]{run();}) {}
    ~LatestWorker() { stop(); }
    void stop() {
        { std::lock_guard lock(mutex_); stopping_=true; pending_.reset(); }
        ready_.notify_one();
        if(thread_.joinable()) thread_.join();
    }
    std::uint64_t submit(Input input) {
        std::lock_guard lock(mutex_);
        auto id=++generation_; pending_=Job{id,std::move(input)}; reply_.reset(); ready_.notify_one(); return id;
    }
    void cancel() { std::lock_guard lock(mutex_); ++generation_; pending_.reset(); reply_.reset(); }
    std::optional<Reply> poll() { std::lock_guard lock(mutex_); return std::exchange(reply_,std::nullopt); }
private:
    struct Job { std::uint64_t generation; Input input; };
    void run() {
        for(;;) {
            std::optional<Job> job;
            { std::unique_lock lock(mutex_); ready_.wait(lock,[&]{return stopping_ || pending_.has_value();});
              if(stopping_) return;
              job=std::exchange(pending_,std::nullopt); }
            auto value=work_(std::move(job->input));
            { std::lock_guard lock(mutex_); if(!stopping_ && job->generation==generation_) reply_=Reply{job->generation,std::move(value)}; }
        }
    }
    std::function<Output(Input)> work_;
    std::mutex mutex_; std::condition_variable ready_;
    bool stopping_=false; std::uint64_t generation_=0;
    std::optional<Job> pending_; std::optional<Reply> reply_;
    std::thread thread_;
};
}
