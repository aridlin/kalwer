#pragma once
#include "appearance.hpp"
namespace kalwer::games {
struct Shop {
    struct Item {const char* name;const char* detail;int cost;};
    static constexpr std::array<Item,5> items{{
        {"Snake: wraparound","A separate arcade rule: cross the edges",150},
        {"Garden: moonlit siege","A longer six-wave garden with stone beds",250},
        {"Chess: walnut board","Warm wood squares and ivory pieces",90},
        {"Peggle: prism board","An extra geometric board with brick arcs",200},
        {"Aurora celebrations","Pink and blue win effects in every game",120}
    }};
    int selected=0;
    std::string message="Unlocks are permanent. Base games stay free.";
    void activate() {
        if(selected<0 || selected>=int(items.size()))return;
        const auto& item=items[selected];
        if(wallet.has(selected))message=wallet.toggle(selected)?(wallet.uses(selected)?"Equipped for the next round.":"Unequipped for the next round."):"Could not save. Nothing changed.";
        else if(wallet.balance<item.cost)message="Need "+std::to_string(item.cost-wallet.balance)+" more koins.";
        else message=wallet.purchase(selected,item.cost)?"Unlocked and equipped! Open a new round.":"Could not save. No koins spent.";
    }
    void key(int key){if(key==3 || key=='w')selected=(selected+4)%5;if(key==4 || key=='s')selected=(selected+1)%5;if(key==13 || key==' ')activate();}
    void pointer(double x,double y,int button){if(button==1 && x>=22 && x<398 && y>=90 && y<390){selected=std::clamp(int((y-90)/60),0,4);activate();}}
    template<class P>void draw(P& p)const {
        p.text(24,57,17,"PERMANENT UNLOCKS",0xffd579);
        p.text(24,77,11,"Buy once. Click an owned item to equip / unequip.",0x8dada1);
        for(int i=0;i<5;i++) {
            double y=90+i*60;p.rect(22,y,376,55,i==selected?0x2b5145:0x112e29);
            if(i==selected)p.line(23,y+2,23,y+53,0x8ce9b3,3);
            p.text(32,y+19,14,items[i].name,0xe0f5e8);
            p.text(32,y+36,10,items[i].detail,0x8dada1);
            p.text(32,y+49,10,wallet.has(i)?(wallet.uses(i)?"EQUIPPED - click to disable":"OWNED - click to equip"):"BUY  "+std::to_string(items[i].cost)+" koins",0xffd579);
        }
        p.text(24,419,11,message,0xe0f5e8);
        p.text(24,465,12,"Arrows: select   Enter: buy / equip",0x8dada1);
    }
};
}
