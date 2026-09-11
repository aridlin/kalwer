#pragma once
#include "appearance.hpp"
namespace kalwer::games {
struct Shop {
    struct Item {const char* name;const char* detail;int cost;};
    static constexpr std::array<Item,9> items{{
        {"Snake: wraparound","A separate arcade rule: cross the edges",3000},
        {"Garden: moonlit siege","A longer six-wave garden with stone beds",7500},
        {"Chess: walnut board","Warm wood squares and ivory pieces",1000},
        {"Peggle: prism board","An extra geometric board with brick arcs",5000},
        {"Aurora celebrations","Pink and blue win effects in every game",1500},
        {"Tetris","Permanent falling-block game",6000},
        {"Breakout","Permanent three-board brick breaker",8000},
        {"Kar","Permanent native phone-style racing",12000},
        {"Koom","Permanent Doom-compatible Freedoom player",25000}
    }};
    int selected=0;
    std::string message="Paid games: free 10-minute daily trials in /games.";
    void activate() {
        if(selected<0 || selected>=int(items.size()))return;
        const auto& item=items[selected];
        if(wallet.has(selected) && selected>=5){message="Owned permanently. Launch it from /games.";return;}
        if(wallet.has(selected))message=wallet.toggle(selected)?(wallet.uses(selected)?"Equipped for the next round.":"Unequipped for the next round."):"Could not save. Nothing changed.";
        else if(wallet.balance<item.cost)message="Need "+std::to_string(item.cost-wallet.balance)+" more koins.";
        else message=wallet.purchase(selected,item.cost)?"Unlocked and equipped! Open a new round.":"Could not save. No koins spent.";
    }
    void key(int key){if(key==3 || key=='w')selected=(selected+8)%9;if(key==4 || key=='s')selected=(selected+1)%9;if(key==13 || key==' ')activate();}
    void pointer(double x,double y,int button){if(button==1 && x>=22 && x<398 && y>=90 && y<402){int row=int((y-90)/52)+(selected/6)*6;if(row>=int(items.size()))return;selected=row;activate();}}
    template<class P>void draw(P& p)const {
        p.text(24,57,17,"PERMANENT UNLOCKS",0xffd579);
        p.text(24,77,11,"Buy once. Toggle owned upgrades on / off freely.",0x8dada1);
        for(int i=(selected/6)*6;i<std::min(9,(selected/6+1)*6);i++) {
            double y=90+(i%6)*52;p.rect(22,y,376,50,i==selected?0x2b5145:0x112e29);
            if(i==selected)p.line(23,y+2,23,y+48,0x8ce9b3,3);
            p.text(32,y+19,14,items[i].name,0xe0f5e8);
            p.text(32,y+36,10,items[i].detail,0x8dada1);
            p.text(32,y+47,10,wallet.has(i)?(i>=5?"OWNED - launch from /games":wallet.uses(i)?"ON - click to disable":"OFF - click to enable"):"BUY  "+std::to_string(items[i].cost)+" koins",0xffd579);
        }
        p.text(24,419,11,message,0xe0f5e8);
        p.text(24,465,12,"Arrows: select / next page   Enter: buy / equip",0x8dada1);
    }
};
}
