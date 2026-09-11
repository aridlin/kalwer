#include "../koom.hpp"
#include <cassert>
#include <iostream>
using namespace kalwer;
int main(){
 auto dir=std::filesystem::temp_directory_path()/"kalwer-koom-runtime-test";std::filesystem::create_directories(dir);wallet.path=dir/"wallet";
 koom::install_bundle=[](const auto& target){std::filesystem::create_directories(target);std::filesystem::copy_file("assets/koom/freedoom2.wad",target/"freedoom2.wad",std::filesystem::copy_options::overwrite_existing);std::filesystem::copy_file("build-koom/kalwer-koom",target/"kalwer-koom",std::filesystem::copy_options::overwrite_existing);return true;};
 auto session=std::make_shared<koom::Session>(std::filesystem::path{});std::array<unsigned char,256> keys{};
 std::this_thread::sleep_for(std::chrono::milliseconds(100));{std::lock_guard lock(session->mutex);assert(session->sequence==0);}
 session->input(true,keys);auto until=std::chrono::steady_clock::now()+std::chrono::seconds(15);bool got=false;
 while(std::chrono::steady_clock::now()<until){{std::lock_guard lock(session->mutex);if(session->sequence>=3){assert(session->pixels.size()==64000);got=true;break;}}std::this_thread::sleep_for(std::chrono::milliseconds(10));}assert(got);
 koom::Game game;game.session=session;wallet.balance=wallet.wins=0;{std::lock_guard lock(session->mutex);session->completed_maps=1;}
 game.focus(true);game.focus(true);assert(wallet.balance==100 && wallet.wins==1);game.session.reset();
 session->input(false,keys);std::this_thread::sleep_for(std::chrono::milliseconds(100));uint64_t paused;{std::lock_guard lock(session->mutex);paused=session->sequence;}
 std::this_thread::sleep_for(std::chrono::milliseconds(150));{std::lock_guard lock(session->mutex);assert(session->sequence==paused);kill(session->process,SIGKILL);}
 session->input(true,keys);std::this_thread::sleep_for(std::chrono::milliseconds(100));session.reset();std::filesystem::remove_all(dir);
 std::cout<<"Bundled Freedoom frames, focus pause and helper-crash isolation passed.\n";
}
