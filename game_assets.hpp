#pragma once
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
namespace kalwer::game_assets {
struct Asset {const char* name;const char* sha256;uintmax_t size;};
inline constexpr Asset kar_art{"art.karp","5737b38fb36c1701735c1434a5ee0c82009f5dd1a6489b958d6d4c87170f9b76",33847352};
inline constexpr Asset kar_scene{"art.kars","e58600f535d646ace74724c4530b90c183d649d1ca50684645dd750aef2d55d1",976208};
inline constexpr Asset freedoom{"freedoom2.wad","a8772e088847032510d97ba2312406a6998f21cbab44d4ff10696faa9c0ecd4b",28787748};
inline constexpr Asset soundfont{"TimGM6mb.sf2","c5378b62028c920cb11e4803327983fee2f2cdff5dc89c708e39da417e51c854",5969788};
inline constexpr const char* base_url="https://github.com/aridlin/kalwer/releases/download/game-data-v1/";
inline std::function<bool(const std::string&,const std::filesystem::path&)> download;
inline std::function<std::string(const std::vector<unsigned char>&)> hash;
inline std::mutex download_mutex;
inline bool valid(const std::filesystem::path& path,const Asset& asset){
    std::error_code ec;if(!hash || std::filesystem::file_size(path,ec)!=asset.size || ec)return false;
    std::ifstream in(path,std::ios::binary);std::vector<unsigned char> bytes(asset.size);
    return bool(in.read(reinterpret_cast<char*>(bytes.data()),bytes.size())) && hash(bytes)==asset.sha256;
}
// Called only by game workers. Files are pinned by size and SHA-256; a partial
// download never becomes the active cache, and unrelated user WADs are untouched.
inline bool ensure(const std::filesystem::path& dir,const Asset& asset){
    std::lock_guard lock(download_mutex);
    auto path=dir/asset.name;auto part=path;part+=".download";
    try {
        if(valid(path,asset))return true;
        if(!download || !hash)return false;
        std::filesystem::create_directories(dir);
        if(!download(std::string(base_url)+asset.name,part) || !valid(part,asset)){
            std::error_code ec;std::filesystem::remove(part,ec);return false;
        }
#ifdef _WIN32
        // The existing file failed validation; replace it only after the new
        // bytes have passed both checks. No valid cache is removed on failure.
        std::filesystem::remove(path);
#endif
        std::filesystem::rename(part,path);return true;
    }catch(...){std::error_code ec;std::filesystem::remove(part,ec);return false;}
}
}
