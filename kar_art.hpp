#pragma once
#include "kar_pixels.hpp"
#include "game_assets.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <atomic>
#include <bit>
#include <thread>

namespace kalwer::games::kar_pixels {
struct Sprite {
    int x=0,y=0,width=0,height=0;
    std::vector<uint32_t> pixels;
    void draw(Surface& surface,int anchor_x,int anchor_y,double scale=1,bool flip=false)const {
        if(!std::isfinite(scale) || scale<=0 || scale>32)return;
        int left=flip?-x-width:x;
        surface.sprite(pixels.data(),width,height,anchor_x+int(std::round(left*scale)),anchor_y+int(std::round(y*scale)),std::max(1,int(std::round(width*scale))),std::max(1,int(std::round(height*scale))),flip);
    }
};
struct ScenePrimitive {
    uint32_t kind=0,color=0,bank=0,frame=0;
    std::array<int32_t,9> vertices{}; // lateral, course distance, elevation
};
struct Art {
    std::map<uint32_t,Sprite> sprites;
    std::vector<ScenePrimitive> scene;
    std::map<int,std::vector<size_t>> scene_segments;
    static std::vector<ScenePrimitive> load_scene(const std::filesystem::path& path){
        std::ifstream in(path,std::ios::binary);if(!in)return {};
        char magic[8];in.read(magic,8);if(in.gcount()!=8 || std::string_view(magic,8)!="KARSC001")return {};
        bool valid=true;auto word=[&](){unsigned char bytes[4]{};if(!in.read(reinterpret_cast<char*>(bytes),4))valid=false;return uint32_t(bytes[0])|(uint32_t(bytes[1])<<8)|(uint32_t(bytes[2])<<16)|(uint32_t(bytes[3])<<24);};
        uint32_t count=word();if(!valid || count>65536)return {};
        std::vector<ScenePrimitive> result;result.reserve(count);
        for(uint32_t i=0;i<count;++i){ScenePrimitive item;item.kind=word();item.color=word();item.bank=word();item.frame=word();
            if(item.kind>3 || item.bank>65535 || item.frame>65535)return {};
            for(auto& coordinate:item.vertices){coordinate=int32_t(word());if(coordinate<-1000000 || coordinate>1000000)return {};}
            if(!valid)return {};
            result.push_back(item);
        }
        if(in.peek()!=std::char_traits<char>::eof())return {};
        return result;
    }
    static constexpr uint32_t key(unsigned bank,unsigned frame){return (bank<<16)|frame;}
    const Sprite* get(unsigned bank,unsigned frame)const{auto it=sprites.find(key(bank,frame));return it==sprites.end()?nullptr:&it->second;}
    bool draw(Surface& surface,unsigned bank,unsigned frame,int x,int y,double scale=1,bool flip=false)const{
        if(auto sprite=get(bank,frame)){sprite->draw(surface,x,y,scale,flip);return true;}return false;
    }
    bool text(Surface& surface,unsigned bank,std::string_view text,int x,int y)const{
        bool any=false;
        for(unsigned char ch:text){if(ch==' '){x+=5;continue;}if(auto glyph=get(bank,1000+ch)){glyph->draw(surface,x,y);x+=glyph->width+1;any=true;}}
        return any;
    }
    static std::shared_ptr<const Art> load(const std::filesystem::path& path){
        std::ifstream in(path,std::ios::binary);if(!in)return {};
        char magic[8];in.read(magic,8);if(in.gcount()!=8 || std::string_view(magic,8)!="KARPX001")return {};
        bool valid=true;
        auto u32=[&](){unsigned char b[4]{};if(!in.read(reinterpret_cast<char*>(b),4))valid=false;return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);};
        uint32_t count=u32();if(!valid || count==0 || count>4096)return {};
        auto result=std::make_shared<Art>();size_t total=0;
        for(uint32_t i=0;i<count;++i){
            uint32_t id=u32();int32_t x=int32_t(u32()),y=int32_t(u32());uint32_t width=u32(),height=u32();
            if(!valid || width==0 || height==0 || width>2048 || height>2048 || x<-4096 || x>4096 || y<-4096 || y>4096)return {};
            size_t amount=size_t(width)*height;total+=amount;if(total>16*1024*1024)return {};
            Sprite sprite;sprite.x=x;sprite.y=y;sprite.width=int(width);sprite.height=int(height);sprite.pixels.resize(amount);
            if(!in.read(reinterpret_cast<char*>(sprite.pixels.data()),std::streamsize(amount*4)))return {};
            if constexpr(std::endian::native==std::endian::big)for(auto& pixel:sprite.pixels)pixel=((pixel&255)<<24)|((pixel&0xff00)<<8)|((pixel>>8)&0xff00)|(pixel>>24);
            if(!valid || !result->sprites.emplace(id,std::move(sprite)).second)return {};
        }
        if(in.peek()!=std::char_traits<char>::eof())return {};
        auto scene_path=path;scene_path.replace_extension("kars");result->scene=load_scene(scene_path);
        for(size_t i=0;i<result->scene.size();++i)result->scene_segments[result->scene[i].vertices[1]/1024].push_back(i);
        return result;
    }
};
struct ArtRequest {
    std::atomic<bool> ready{false};
    std::shared_ptr<const Art> result;
    static std::shared_ptr<ArtRequest> start(std::filesystem::path path){
        auto request=std::make_shared<ArtRequest>();
        std::thread([request,path=std::move(path)](){
            try{
                request->result=Art::load(path);
                // An interrupted first download may leave the validated sprite
                // file without its scene. Finish that pair before exposing it.
                if(request->result && request->result->scene.empty() && game_assets::valid(path,game_assets::kar_art)) {
                    if(game_assets::ensure(path.parent_path(),game_assets::kar_scene))request->result=Art::load(path);
                    else request->result.reset();
                }
                if(!request->result && game_assets::ensure(path.parent_path(),game_assets::kar_art) && game_assets::ensure(path.parent_path(),game_assets::kar_scene))request->result=Art::load(path);
            }catch(...){request->result.reset();}
            request->ready.store(true,std::memory_order_release);
        }).detach();
        return request;
    }
    const Art* get()const{return ready.load(std::memory_order_acquire)?result.get():nullptr;}
};
inline std::string import_art(const std::filesystem::path& source,const std::filesystem::path& target){
    try {
        auto art=Art::load(source);
        if(!art)return "Invalid or unreadable Kar art pack.";
        auto scene=source;scene.replace_extension("kars");
        if(std::filesystem::exists(scene) && art->scene.empty())return "Invalid or empty Kar scene pack.";
        if(std::filesystem::exists(target/"art.karp") && std::filesystem::equivalent(source,target/"art.karp"))return "This pack is already installed. Reopen /kar.";
        auto stage=target;stage+=".import";auto backup=target;backup+=".previous";
        if(std::filesystem::exists(stage) || std::filesystem::exists(backup))return "A previous art import needs attention. Existing artwork was kept.";
        std::filesystem::create_directories(stage);
        try {
            std::filesystem::copy_file(source,stage/"art.karp");
            if(std::filesystem::exists(scene))std::filesystem::copy_file(scene,stage/"art.kars");
            bool existed=std::filesystem::exists(target);
            if(existed)std::filesystem::rename(target,backup);
            try{std::filesystem::rename(stage,target);}catch(...){if(existed)std::filesystem::rename(backup,target);throw;}
            std::error_code ec;if(existed)std::filesystem::remove_all(backup,ec);
        }catch(...){std::error_code ec;std::filesystem::remove_all(stage,ec);throw;}
        return "Kar artwork installed. Reopen /kar to use it.";
    }catch(...){return "Could not install Kar artwork. Check the file path and permissions.";}
}
}
