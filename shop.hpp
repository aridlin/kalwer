#pragma once
#include "appearance.hpp"
namespace kalwer::games {
struct Shop {
    struct Item {const char* name;const char* detail;int cost;};
    static constexpr std::array<Item,Wallet::item_count> items{{
        {"Snake: wraparound","A separate arcade rule: cross the edges",3000},
        {"Garden: moonlit siege","A longer six-wave garden with stone beds",7500},
        {"Chess: walnut board","Warm wood squares and ivory pieces",1000},
        {"Peggle: prism board","An extra geometric board with brick arcs",5000},
        {"Aurora celebrations","Pink and blue win effects in every game",1500},
        {"Tetris","Permanent falling-block game",6000},
        {"Breakout","Permanent three-board brick breaker",8000},
        {"Kar","Permanent native phone-style racing",12000},
        {"Koom","Permanent Doom-compatible Freedoom player",25000},
        {"Tetris: hold piece","C: store or swap one piece per drop",4500},
        {"Tetris: breathing room","Gravity falls 20% slower",6500},
        {"Breakout: wide paddle","A 25% wider paddle",6000},
        {"Breakout: spare ball","Start each run with four lives",4500},
        {"Kar: nitro reserve","Start races with 75 nitro instead of 25",8000},
        {"Kar: quick recovery","Recover from crashes in half the time",10000},
        {"Koom: field kit","Start each map with 150 health and armor",14000},
        {"Koom: bounty contract","25 extra koins per completed map",18000}
    }};
    int selected=0;
    std::string message="Paid games: free 10-minute daily trials in /games.";
    void activate() {
        if(selected<0 || selected>=int(items.size()))return;
        const auto& item=items[selected];
        if(wallet.has(selected) && selected>=5 && selected<=8){message="Owned permanently. Launch it from /games.";return;}
        if(wallet.has(selected))message=wallet.toggle(selected)?(wallet.uses(selected)?"Equipped for the next round.":"Unequipped for the next round."):"Could not save. Nothing changed.";
        else if(wallet.balance<item.cost)message="Need "+std::to_string(item.cost-wallet.balance)+" more koins.";
        else message=wallet.purchase(selected,item.cost)?"Unlocked and equipped! Open a new round.":"Could not save. No koins spent.";
    }
    void key(int key){if(key==3 || key=='w')selected=(selected+int(items.size())-1)%items.size();if(key==4 || key=='s')selected=(selected+1)%items.size();if(key==13 || key==' ')activate();}
    void pointer(double x,double y,int button){
        if(button==1 && y>=430 && y<450){int pages=(int(items.size())+5)/6;if(x>=24 && x<124)selected=((selected/6+pages-1)%pages)*6;else if(x>=296 && x<396)selected=((selected/6+1)%pages)*6;return;}
        if(button==1 && x>=22 && x<398 && y>=90 && y<402){int row=int((y-90)/52)+(selected/6)*6;if(row>=int(items.size()))return;selected=row;activate();}}
    template<class P>void draw(P& p)const {
        p.text(24,57,17,"PERMANENT UNLOCKS",0xffd579);
        p.text(24,77,11,"Buy once. Toggle owned upgrades on / off freely.",0x8dada1);
        for(int i=(selected/6)*6;i<std::min(int(items.size()),(selected/6+1)*6);i++) {
            double y=90+(i%6)*52;p.rect(22,y,376,50,i==selected?0x2b5145:0x112e29);
            if(i==selected)p.line(23,y+2,23,y+48,0x8ce9b3,3);
            p.text(32,y+19,14,items[i].name,0xe0f5e8);
            p.text(32,y+36,10,items[i].detail,0x8dada1);
            p.text(32,y+47,10,wallet.has(i)?(i>=5 && i<=8?"OWNED - launch from /games":wallet.uses(i)?"ON - click to disable":"OFF - click to enable"):"BUY  "+std::to_string(items[i].cost)+" koins",0xffd579);
        }
        p.text(24,419,11,message,0xe0f5e8);
        p.rect(24,430,100,20,0x2b5145);p.text(36,445,11,"Previous",0xe0f5e8);
        p.rect(296,430,100,20,0x2b5145);p.text(330,445,11,"Next",0xe0f5e8);
        p.text(177,445,11,std::to_string(selected/6+1)+" / "+std::to_string((items.size()+5)/6),0x8dada1);
        p.text(24,465,12,"Arrows: select / next page   Enter: buy / equip",0x8dada1);
    }
};
}
