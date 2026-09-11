#include "../koom.hpp"
#include <cassert>
#include <iostream>
using namespace kalwer;
int main(int argc,char** argv){
 if(argc>1){
  std::array<unsigned char,256> keys{};std::vector<uint32_t> pixels(320*200);
  uint32_t header[]={0x4b4f4f4d,320,200,0};
  while(fread(keys.data(),1,keys.size(),stdin)==keys.size()){
   std::this_thread::sleep_for(std::chrono::milliseconds(12));
   fwrite(header,4,4,stdout);fwrite(pixels.data(),4,pixels.size(),stdout);fflush(stdout);
  }return 0;
 }
 auto dir=std::filesystem::temp_directory_path()/("kalwer-pacing-"+std::to_string(getpid()));
 std::filesystem::create_directories(dir);wallet.path=dir/"wallet";
 auto executable=std::filesystem::canonical(argv[0]);
 koom::install_bundle=[&](const auto& target){
  std::filesystem::create_directories(target);
  std::filesystem::copy_file(executable,target/"kalwer-koom");
  std::filesystem::copy_file("assets/koom/freedoom2.wad",target/"freedoom2.wad");return true;
 };
 {
  auto session=std::make_shared<koom::Session>(std::filesystem::path{});
  session->input(true,{});
  auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  std::chrono::steady_clock::time_point first{};
  for(;;){
   uint64_t count;{std::lock_guard lock(session->mutex);count=session->sequence;}
   auto now=std::chrono::steady_clock::now();assert(now<limit);
   if(count>=1 && first==std::chrono::steady_clock::time_point{})first=now;
   if(count>=101){
    double elapsed=std::chrono::duration<double>(now-first).count();
    assert(elapsed>2.65 && elapsed<3.65);
    std::cout<<"100 paced frames with 12 ms render latency: "<<elapsed<<" seconds\n";break;
   }
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
 }
 std::filesystem::remove_all(dir);
}
