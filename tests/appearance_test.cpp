#include "../games.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace kalwer;
int main(){
    auto dir=std::filesystem::temp_directory_path()/("kalwer-style-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Appearance a;a.directory=dir;a.mode=2;a.theme=3;a.opacity=60;a.scale=4;a.popup_mode=3;a.bw=true;a.keep_halftone=false;assert(a.save());
    Appearance b;b.directory=dir;assert(b.load());assert(b.mode==2 && b.theme==3 && b.opacity==60 && b.scale==4 && b.popup_mode==3 && b.bw && !b.keep_halftone && b.popup_keep_halftone);
    assert(a.save("preset.ini"));b.theme=1;assert(b.load("preset.ini") && b.theme==3);
    atomic_text(dir/"bad.ini","theme=nope");assert(!b.load("bad.ini") && b.theme==3);
    std::vector<uint32_t> gradient(64*64);for(int y=0;y<64;++y)for(int x=0;x<64;++x){unsigned v=x*4;gradient[y*64+x]=0xff000000|v<<16|v<<8|v;}
    std::vector<std::vector<uint32_t>> images;
    for(int mode=1;mode<=5;++mode){auto image=dither_image(gradient,64,64,mode,0);int whites=0;for(auto c:image){unsigned dark=mode==5?0xff000000:0xff000000|themes[0].background,light=mode==5?0xffffffff:0xff000000|themes[0].text;assert(c==dark || c==light);whites+=c==light;}assert(whites>1000 && whites<3000);for(auto& old:images)assert(image!=old);images.push_back(image);}
    wallet.path=dir/"koins-v1";assert(wallet.credit(10));wallet.balance=0;wallet.load();assert(wallet.balance==10);
    games::Game g(games::Kind::minesweeper,1);g.focused=true;g.over=g.won=true;
    g.tick(.02);
    assert(wallet.balance==40 && g.reward_claimed && g.reward==30);
    for(int i=0;i<100;++i)g.tick(.02);
    assert(wallet.balance==40);
    Wallet restored;restored.path=wallet.path;restored.load();assert(restored.balance==40 && restored.wins==2);
    g.reset();assert(!g.reward_claimed && wallet.balance==40);
    std::filesystem::remove_all(dir);
    std::cout<<"Dither algorithms, presets, persistent koins and one reward per win passed.\n";
}
