#include "../latest_worker.hpp"
#include <cassert>
#include <future>
#include <chrono>
#include <iostream>
using namespace std::chrono_literals;
int main() {
    std::promise<void> started,release;
    auto gate=release.get_future().share();
    kalwer::LatestWorker<int,int> worker([&](int value) {
        if(value==1) {started.set_value();gate.wait();}
        return value*10;
    });
    worker.submit(1); started.get_future().wait();
    worker.submit(2); auto newest=worker.submit(3);
    release.set_value();
    auto deadline=std::chrono::steady_clock::now()+3s;
    std::optional<kalwer::LatestWorker<int,int>::Reply> reply;
    while(!(reply=worker.poll()) && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    assert(reply && reply->generation==newest && reply->value==30);
    worker.submit(4);worker.cancel();
    assert(!worker.poll());
    worker.stop();worker.stop();
    std::promise<void> started2,release2;
    auto gate2=release2.get_future().share();
    kalwer::LatestWorker<int,int> canceled([&](int n){started2.set_value();gate2.wait();return n;});
    canceled.submit(5);started2.get_future().wait();canceled.cancel();release2.set_value();canceled.stop();
    assert(!canceled.poll());
    std::cout<<"Latest request wins, pending work coalesces, cancellation and joined shutdown passed.\n";
}
