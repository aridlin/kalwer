#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace kalwer {
inline bool atomic_text(const std::filesystem::path& path,const std::string& value) {
    std::error_code ec; std::filesystem::create_directories(path.parent_path(),ec);
    if(ec) return false;
    auto temporary=path; temporary+=".tmp";
    { std::ofstream out(temporary,std::ios::binary|std::ios::trunc); out<<value; out.flush(); if(!out) return false; }
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
#else
    std::filesystem::permissions(temporary,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write,ec);
    std::filesystem::rename(temporary,path,ec); return !ec;
#endif
}
struct Theme { const char* name; unsigned background,accent,text; };
inline constexpr std::array<Theme,6> themes{{
    {"Forest",0x081a18,0x8ce9b3,0xe0f5e8},{"Amber",0x20170b,0xf4bb65,0xffefcf},
    {"Glacier",0x0d1928,0x83d3f7,0xe0f1ff},{"Rose",0x241420,0xf1a0c5,0xffe4f0},
    {"Violet",0x19142a,0xbba1f6,0xeee7ff},{"Mono",0x191919,0xd0d0d0,0xf4f4f4}}};
inline constexpr std::array<const char*,6> dithers{"Halftone","Atkinson","Floyd-Steinberg","Bayer 4x4","Bayer 8x8","Threshold"};
struct Appearance {
    int mode=0,popup_mode=0,theme=0,opacity=72,scale=1;
    bool bw=false,keep_halftone=true,popup_keep_halftone=true;
    std::filesystem::path directory;
    void clamp() { mode=std::clamp(mode,0,5); popup_mode=std::clamp(popup_mode,0,5); theme=std::clamp(theme,0,5); opacity=std::clamp(opacity,30,95); scale=std::clamp(scale,1,8); }
    bool load(const char* name="appearance.ini") {
        std::ifstream in(directory/name); if(!in) return false;
        Appearance next=*this; std::string line;
        while(std::getline(in,line)) { auto split=line.find('='); if(split==std::string::npos) continue;
            try { int v=std::stoi(line.substr(split+1)); auto key=line.substr(0,split);
                if(key=="dither") next.mode=v; else if(key=="popup_dither") next.popup_mode=v; else if(key=="backdrop_bw") next.bw=v!=0; else if(key=="keep_halftone") next.keep_halftone=v!=0; else if(key=="popup_keep_halftone") next.popup_keep_halftone=v!=0; else if(key=="theme") next.theme=v; else if(key=="opacity") next.opacity=v; else if(key=="dither_scale") next.scale=v;
            } catch(...) { return false; }
        }
        next.clamp(); *this=next; return true;
    }
    bool save(const char* name="appearance.ini") const {
        return atomic_text(directory/name,"version=1\ndither="+std::to_string(mode)+"\npopup_dither="+std::to_string(popup_mode)+"\nbackdrop_bw="+std::to_string(bw)+"\nkeep_halftone="+std::to_string(keep_halftone)+"\npopup_keep_halftone="+std::to_string(popup_keep_halftone)+"\ntheme="+std::to_string(theme)+"\nopacity="+std::to_string(opacity)+"\ndither_scale="+std::to_string(scale)+"\n");
    }
    unsigned tint(unsigned c) const {
        if(theme==0) return c;
        unsigned r=(c>>16)&255,g=(c>>8)&255,b=c&255;
        // Theme neutral/green UI surfaces; gameplay's orange, purple and blue retain their meaning.
        if(g<r*.92 || g<b*.9) return c;
        double value=std::max({r,g,b})/255.; unsigned target=value<.24?themes[theme].background:value>.87?themes[theme].text:themes[theme].accent;
        double gain=value<.24?std::clamp(value/.102,.45,2.):value>.87?1.:std::clamp(value/.914,.2,1.);
        unsigned result=0; for(int shift:{0,8,16}) result|=unsigned(std::min(255.,((target>>shift)&255)*gain))<<shift;
        return result;
    }
};
inline Appearance appearance;
inline std::string theme_css(std::string css) {
    for(size_t i=0;i+7<=css.size();++i)if(css[i]=='#') {
        bool hex=true;for(size_t j=1;j<=6;++j)hex=hex && std::isxdigit(static_cast<unsigned char>(css[i+j]));
        if(!hex || (i+7<css.size() && std::isxdigit(static_cast<unsigned char>(css[i+7]))))continue;
        unsigned value=unsigned(std::stoul(css.substr(i+1,6),nullptr,16));char text[8];std::snprintf(text,sizeof(text),"#%06x",appearance.tint(value));css.replace(i,7,text);i+=6;
    }
    return css;
}
struct Wallet {
    static constexpr int item_count=17;
    static constexpr std::uint64_t game_mask=15ULL<<5, item_mask=(1ULL<<item_count)-1;
    std::filesystem::path path;
    std::int64_t balance=0,wins=0;
    std::uint64_t owned=0,equipped=0;
    void load() {
        std::ifstream in(path); std::int64_t b=0,w=0;
        if(in>>b>>w && b>=0 && b<1000000000000LL && w>=0) {
            balance=b;wins=w;owned=equipped=0;
            int version=0;std::uint64_t o=0,e=0;
            if(in>>version>>o>>e && version==2){owned=o&item_mask;equipped=e&owned;}
        }
    }
    bool save(std::int64_t b,std::int64_t w,std::uint64_t o,std::uint64_t e) {
        if(path.empty() || !atomic_text(path,std::to_string(b)+" "+std::to_string(w)+"\n2 "+std::to_string(o)+" "+std::to_string(e)+"\n"))return false;
        balance=b;wins=w;owned=o;equipped=e;return true;
    }
    bool has(int id) const {return id>=0 && id<item_count && (owned&(1ULL<<id));}
    bool uses(int id) const {return has(id) && (equipped&(1ULL<<id));}
    bool purchase(int id,int cost) {
        if(id<0 || id>=item_count || cost<=0 || has(id) || balance<cost)return false;
        return save(balance-cost,wins,owned|(1ULL<<id),equipped|(1ULL<<id));
    }
    bool unlock_games(){return save(balance,wins,owned|game_mask,equipped|game_mask);}
    bool toggle(int id) {return has(id) && save(balance,wins,owned,equipped^(1ULL<<id));}
    bool credit(int amount, bool victory=true) {
        if(amount<=0 || balance>999999999999LL-amount) return false;
        return save(balance+amount,wins+(victory?1:0),owned,equipped);
    }
};
inline Wallet wallet;
// Input/output are opaque 0xAARRGGBB pixels. Diffusion is sequential, not an ordered-noise approximation.
inline std::vector<std::uint32_t> dither_image(const std::vector<std::uint32_t>& pixels,int width,int height,int mode,int theme) {
    if(width<=0 || height<=0 || pixels.size()!=size_t(width)*height) return {};
    std::vector<float> values(pixels.size());
    for(size_t i=0;i<pixels.size();++i) values[i]=(.2126*((pixels[i]>>16)&255)+.7152*((pixels[i]>>8)&255)+.0722*(pixels[i]&255))/255.;
    std::vector<std::uint32_t> out(pixels.size());
    constexpr int bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    auto add=[&](int x,int y,float e){if(x>=0 && y>=0 && x<width && y<height) values[size_t(y)*width+x]+=e;};
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        size_t i=size_t(y)*width+x; float v=values[i],threshold=.5f;
        if(mode==3) threshold=(bayer[(y%4)*4+x%4]+.5f)/16;
        if(mode==4) threshold=(4*bayer[(y%4)*4+x%4]+bayer[(y/4%2)*4+x/4%2]/4.f+.5f)/64;
        bool white=v>=threshold; out[i]=0xff000000|(mode==5?(white?0xffffff:0):(white?themes[theme].text:themes[theme].background));
        float e=v-(white?1:0);
        if(mode==1) { e/=8; add(x+1,y,e);add(x+2,y,e);add(x-1,y+1,e);add(x,y+1,e);add(x+1,y+1,e);add(x,y+2,e); }
        if(mode==2) { add(x+1,y,e*7/16);add(x-1,y+1,e*3/16);add(x,y+1,e*5/16);add(x+1,y+1,e/16); }
    }
    return out;
}
}
