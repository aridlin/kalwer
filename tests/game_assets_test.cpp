#include "../game_assets.hpp"
#include <cassert>
#include <iostream>
using namespace kalwer::game_assets;
int main(){
 auto dir=std::filesystem::temp_directory_path()/"kalwer-game-assets-test";
 std::filesystem::remove_all(dir);std::filesystem::create_directories(dir);
 Asset asset{"sample.dat","verified",4};int requests=0;bool corrupt=false;
 hash=[](const std::vector<unsigned char>& bytes){return std::string(bytes.begin(),bytes.end())=="DATA"?"verified":"wrong";};
 download=[&](const std::string& url,const auto& target){assert(url==std::string(base_url)+"sample.dat");++requests;std::ofstream out(target,std::ios::binary);out<<(corrupt?"FAIL":"DATA");return bool(out);};
 assert(requests==0);assert(ensure(dir,asset));assert(requests==1);
 assert(ensure(dir,asset));assert(requests==1); // cached, including offline
 download={};assert(ensure(dir,asset));assert(requests==1);
 std::ofstream(dir/asset.name)<<"BAD";assert(!ensure(dir,asset));
 download=[&](const std::string&,const auto& target){++requests;std::ofstream(target)<<"FAIL";return true;};
 assert(!ensure(dir,asset));assert(!std::filesystem::exists(dir/"sample.dat.download"));
 assert(std::filesystem::file_size(dir/asset.name)==3); // no unverified replacement
 download=[&](const std::string&,const auto& target){++requests;std::ofstream(target)<<"DATA";return true;};
 assert(ensure(dir,asset));assert(valid(dir/asset.name,asset));
 std::filesystem::remove_all(dir);
 std::cout<<"First-use assets: deferred fetch, verified cache, offline reuse, corrupt rejection and retry passed.\n";
}
