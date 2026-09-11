// Real WinHTTP/cache integration, executed on the disposable Windows CI account.
#define wWinMain kalwer_application_entry
#include "../windows/kalwer_windows.cpp"
#undef wWinMain
#include <cassert>
#include <iostream>
int main(){
 load_settings();
 auto root=std::filesystem::temp_directory_path()/L"kalwer-first-use-zażółć";
 std::filesystem::remove_all(root);
 auto artwork=kalwer::games::kar_pixels::ArtRequest::start(root/"kar"/"art.karp");
 auto limit=std::chrono::steady_clock::now()+std::chrono::minutes(4);
 while(!artwork->ready.load()){
  assert(std::chrono::steady_clock::now()<limit);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
 }
 auto art=artwork->get();assert(art && art->get(1,4) && art->get(2100,1049) && !art->scene.empty());
 using namespace kalwer::game_assets;
 assert(ensure(root/"koom",freedoom));assert(ensure(root/"koom",soundfont));
 download=[](const std::string&,const std::filesystem::path&){assert(false && "Cached assets must not request network access");return false;};
 assert(ensure(root/"kar",kar_art));assert(ensure(root/"kar",kar_scene));
 assert(ensure(root/"koom",freedoom));assert(ensure(root/"koom",soundfont));
 std::filesystem::remove_all(root);
 std::cout<<"Windows first-use Kar/Freedoom/SoundFont downloads, original art decoding, Unicode paths and offline cache passed.\n";
}
