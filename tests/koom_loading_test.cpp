#include "../koom.hpp"
#include <cassert>
#include <iostream>
int main(){
 using namespace kalwer;using namespace std::chrono;
 struct State {std::mutex lock;std::condition_variable changed;bool started=false,release=false,finished=false;};
 auto state=std::make_shared<State>();
 koom::install_bundle=[state](const auto&){std::unique_lock lock(state->lock);state->started=true;state->changed.notify_all();state->changed.wait(lock,[&]{return state->release;});state->finished=true;state->changed.notify_all();return false;};
 auto session=std::make_unique<koom::Session>(std::filesystem::path{});
 {std::unique_lock lock(state->lock);assert(state->changed.wait_for(lock,seconds(3),[&]{return state->started;}));}
 auto start=steady_clock::now();session.reset();assert(steady_clock::now()-start<milliseconds(200));
 {std::unique_lock lock(state->lock);assert(!state->finished);state->release=true;state->changed.notify_all();assert(state->changed.wait_for(lock,seconds(3),[&]{return state->finished;}));}
 // Synchronize with the detached preparation task before static teardown.
 {std::lock_guard lock(koom::preparation_mutex);}
 koom::install_bundle={};std::cout<<"Closing Koom does not wait for a stalled game-data download.\n";
}
