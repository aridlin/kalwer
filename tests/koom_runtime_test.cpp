#include "../games.hpp"
#include <cassert>
#include <iostream>
using namespace kalwer;
int main(){
 auto dir=std::filesystem::temp_directory_path()/"kalwer-koom-runtime-test";std::filesystem::remove_all(dir);std::filesystem::create_directories(dir);wallet.path=dir/"wallet";
 auto capture=dir/"audio.f32";setenv("KALWER_KOOM_AUDIO_CAPTURE",capture.c_str(),1);
 koom::install_bundle=[](const auto& target){std::filesystem::create_directories(target);std::filesystem::copy_file("assets/koom/TimGM6mb.sf2",target/"TimGM6mb.sf2",std::filesystem::copy_options::overwrite_existing);std::filesystem::copy_file("assets/koom/freedoom2.wad",target/"freedoom2.wad",std::filesystem::copy_options::overwrite_existing);std::filesystem::copy_file("build-koom/kalwer-koom",target/"kalwer-koom",std::filesystem::copy_options::overwrite_existing);return true;};
 auto session=std::make_shared<koom::Session>(std::filesystem::path{});std::array<unsigned char,256> keys{};
 std::this_thread::sleep_for(std::chrono::milliseconds(100));{std::lock_guard lock(session->mutex);assert(session->sequence==0);}
 session->input(true,keys);auto until=std::chrono::steady_clock::now()+std::chrono::seconds(15);bool got=false;
 while(std::chrono::steady_clock::now()<until){{std::lock_guard lock(session->mutex);if(session->sequence>=3){assert(session->pixels.size()==64000);got=true;break;}}std::this_thread::sleep_for(std::chrono::milliseconds(10));}assert(got);
 koom::Game game;game.session=session;wallet.balance=wallet.wins=0;{std::lock_guard lock(session->mutex);session->completed_maps=1;}
 game.focus(true);game.focus(true);assert(wallet.balance==100 && wallet.wins==1);game.session.reset();
 // Expiring the daily trial must pause the actual helper and audio, not just
 // cover its still-running framebuffer with a launcher overlay.
 wallet.owned=0;games::Game trial(games::Kind::koom,42);trial.trial_active=true;
 trial.koom.session=session;trial.koom.rewarded_maps=1;
 assert(game_trials.begin(3));game_trials.consume(3,599.99);trial.focus(true);trial.tick(.02);
 assert(!trial.playable() && !trial.trial_active);
 std::this_thread::sleep_for(std::chrono::milliseconds(100));uint64_t paused;{std::lock_guard lock(session->mutex);paused=session->sequence;}
 auto paused_audio=std::filesystem::file_size(capture);
 std::this_thread::sleep_for(std::chrono::milliseconds(150));assert(std::filesystem::file_size(capture)==paused_audio);{std::lock_guard lock(session->mutex);assert(session->sequence==paused);kill(session->process,SIGKILL);}
 trial.koom.session.reset();session->input(true,keys);std::this_thread::sleep_for(std::chrono::milliseconds(100));session.reset();std::filesystem::remove_all(dir);
 std::cout<<"Bundled Freedoom frames, trial expiry/audio pause and helper-crash isolation passed.\n";
}
