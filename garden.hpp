#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <string>
#include <vector>
namespace kalwer::games {
// Original vector art and a compact lane-defense ruleset. No assets from PvZ.
struct Garden {
    struct Plant {int type=-1;double hp=0,clock=0,flash=0;};
    struct Zombie {int row=0,type=0;double x=0,hp=0,slow=0,flash=0,walk=0;};
    struct Shot {int row;double x;bool ice;};
    struct Sun {double x,y,age=0;};
    struct Spark {double x,y,age;unsigned color;};
    static constexpr std::array<int,5> costs{50,100,75,150,125};
    static constexpr std::array<const char*,5> names{"SUN","PEA","WALL","FROST","BURST"};
    std::array<Plant,40> plants{};
    std::array<bool,40> stones{};
    std::array<double,5> cooldown{},mowers{};
    std::vector<Zombie> zombies;
    std::vector<Shot> shots;
    std::vector<Sun> suns;
    std::vector<Spark> sparks;
    std::mt19937 random{1};
    bool started=false,over=false,won=false,night=false,shovel=false;
    int sun=200,selected=1,cursor=16,wave=0,spawned=0,kills=0,warning_row=0;
    double clock=0,spawn_clock=7,sky_clock=3;
    std::string message="1-5: seed   Click: plant / collect sun";
    void reset(unsigned seed,bool moon=false) {
        *this=Garden();random.seed(seed);night=moon;mowers.fill(-1);
        if(night){stones[10]=stones[22]=stones[34]=true;sun=225;}
        warning_row=int(random()%5);
    }
    int waves()const{return night?6:4;}
    int wave_size()const{return 6+wave*2;}
    void collect(size_t i){sun+=25;sparks.push_back({suns[i].x,suns[i].y,0,0xffd579});suns.erase(suns.begin()+std::ptrdiff_t(i));}
    void place(int cell) {
        if(over || cell<0 || cell>=40)return;
        cursor=cell;
        if(shovel){plants[cell]=Plant{};message="Bed cleared. No refund.";return;}
        if(stones[cell]){message="Stone bed - plant somewhere else.";return;}
        if(plants[cell].type>=0){message="Occupied. X selects the shovel.";return;}
        if(cooldown[selected]>0){message="Seed packet is recharging.";return;}
        if(sun<costs[selected]){message="Need more sun. Space collects all suns.";return;}
        sun-=costs[selected];plants[cell]={selected,selected==2?480.:100.,selected==0?6.:selected==4?1.:.3,0};
        cooldown[selected]=selected==4?20:3;started=true;message="Space: collect suns   X: shovel";
    }
    void key(int key) {
        if(over)return;
        if(key>='1' && key<='5'){selected=key-'1';shovel=false;}
        if(key=='x')shovel=!shovel;
        if(key==1 || key=='a')cursor=cursor/8*8+(cursor%8+7)%8;
        if(key==2 || key=='d')cursor=cursor/8*8+(cursor%8+1)%8;
        if(key==3 || key=='w')cursor=(cursor+32)%40;
        if(key==4 || key=='s')cursor=(cursor+8)%40;
        if(key==13)place(cursor);
        if(key==' '){started=true;while(!suns.empty())collect(0);}
    }
    void pointer(double x,double y,int button) {
        if(over || (button!=1 && button!=3))return;
        if(button==1)for(size_t i=0;i<suns.size();i++)if(std::hypot(x-suns[i].x,y-suns[i].y)<17){collect(i);return;}
        if(button==1 && x>=24 && x<394 && y>=80 && y<125){selected=std::min(4,int((x-24)/74));shovel=false;return;}
        if(x>=34 && x<386 && y>=141 && y<411){int cell=int((y-141)/54)*8+int((x-34)/44);if(button==3){plants[cell]=Plant{};return;}place(cell);}
    }
    void advance(double dt) {
        if(!started || over)return;
        dt=std::min(dt,.05);clock+=dt;
        for(auto& c:cooldown)c=std::max(0.,c-dt);
        for(auto& s:sparks)s.age+=dt;
        std::erase_if(sparks,[](const Spark& s){return s.age>.7;});
        for(auto& s:suns){s.age+=dt;s.y+=dt*3;}
        std::erase_if(suns,[](const Sun& s){return s.age>12;});
        sky_clock-=dt;
        if(sky_clock<=0){sky_clock=6.5;if(suns.size()<35)suns.push_back({60.+double(random()%300),155.+double(random()%220),0});}
        spawn_clock-=dt;
        if(spawned<wave_size() && spawn_clock<=0) {
            int type=wave>=1 && spawned%4==3?1:wave>=2 && spawned%3==1?2:0;
            zombies.push_back({warning_row,type,401.,type==1?180.:type==2?65.:85.,0,0,0});
            ++spawned;spawn_clock=std::max(2.,4.4-wave*.4);warning_row=int(random()%5);
        }
        for(int i=0;i<40;i++) {
            auto& p=plants[i];if(p.type<0)continue;
            p.flash=std::max(0.,p.flash-dt);p.clock-=dt;
            int row=i/8;double x=56+(i%8)*44,y=168+row*54;
            if(p.type==0 && p.clock<=0){p.clock=8;if(suns.size()<35)suns.push_back({x,y-12,0});}
            if((p.type==1 || p.type==3) && p.clock<=0 && std::any_of(zombies.begin(),zombies.end(),[&](const Zombie& z){return z.row==row && z.x>x && z.hp>0;})){
                shots.push_back({row,x+14,p.type==3});p.clock=p.type==3?2.2:1.35;p.flash=.13;
            }
            if(p.type==4 && p.clock<=0){
                for(auto& z:zombies)if(std::abs(z.row-row)<=1 && std::abs(z.x-x)<86){z.hp-=240;z.flash=.2;}
                for(int j=0;j<14;j++){double a=j*2.399;sparks.push_back({x+std::cos(a)*28,y+std::sin(a)*22,0,0xffa657});}
                p=Plant{};
            }
        }
        for(auto& s:shots) {
            double from=s.x;s.x+=dt*(s.ice?155:195);
            Zombie* hit=nullptr;
            for(auto& z:zombies)if(z.hp>0 && z.row==s.row && z.x+11>=from && z.x-11<=s.x && (!hit || z.x<hit->x))hit=&z;
            if(hit){hit->hp-=s.ice?16:22;hit->flash=.12;if(s.ice)hit->slow=3;s.x=500;}
        }
        std::erase_if(shots,[](const Shot& s){return s.x>430;});
        for(int row=0;row<5;row++)if(mowers[row]>=0 && mowers[row]<450){mowers[row]+=260*dt;for(auto& z:zombies)if(z.row==row && std::abs(z.x-mowers[row])<22)z.hp=0;}
        for(auto& z:zombies) {
            if(z.hp<=0)continue;
            z.slow=std::max(0.,z.slow-dt);z.flash=std::max(0.,z.flash-dt);z.walk+=dt*(z.slow>0?2:4);
            int col=int(std::floor((z.x-34)/44));bool biting=false;
            if(col>=0 && col<8){auto& p=plants[z.row*8+col];if(p.type>=0 && z.x<=56+col*44+24){p.hp-=dt*28;p.flash=.08;biting=true;if(p.hp<=0)p=Plant{};}}
            if(!biting)z.x-=dt*(z.type==2?16.:9.+wave*.5)*(z.slow>0?.48:1.);
            if(z.x<36 && mowers[z.row]<0)mowers[z.row]=22;
            if(z.x<12){over=true;message="The garden was overrun.";}
        }
        std::erase_if(zombies,[&](const Zombie& z){if(z.hp>0)return false;++kills;sparks.push_back({z.x,168.+z.row*54,0,0x8ce9b3});return true;});
        if(spawned>=wave_size() && zombies.empty() && !over){
            if(++wave>=waves()){over=won=true;message="Every wave defended!";}
            else {spawned=0;spawn_clock=7;sun+=50;message="Wave cleared! +50 sun. Replant now.";}
        }
    }
    template<class P>void draw(P& p)const {
        unsigned leaf=night?0xa3bded:0x8ce9b3;
        p.text(24,55,14,"SUN "+std::to_string(sun)+"   WAVE "+std::to_string(std::min(wave+1,waves()))+" / "+std::to_string(waves())+"   "+(night?"MOON":"MEADOW"),0xffd579);
        p.text(24,73,10,message,0xe0f5e8);
        for(int i=0;i<5;i++){
            double x=24+i*74;p.rect(x,80,70,44,selected==i && !shovel?0x2b5145:0x112e29);
            p.text(x+5,96,11,std::to_string(i+1)+" "+names[i],i==selected?leaf:0xe0f5e8);
            p.text(x+5,115,12,cooldown[i]>0?std::to_string(int(std::ceil(cooldown[i])))+"s":std::to_string(costs[i]),sun>=costs[i]?0xffd579:0x8dada1);
        }
        for(int row=0;row<5;row++) {
            p.rect(34,141+row*54,352,52,row%2?0x112e29:0x183b32);
            for(int col=0;col<8;col++)p.line(34+col*44,144+row*54,34+col*44,190+row*54,0x29483b,1);
            if(mowers[row]<0){p.rect(17,168+row*54,13,10,0xe57373);p.circle(20,180+row*54,3,0xe0f5e8);p.circle(29,180+row*54,3,0xe0f5e8);}
            else if(mowers[row]<450){p.rect(mowers[row]-9,158+row*54,22,15,0xe57373);p.circle(mowers[row]-6,177+row*54,4,0xe0f5e8);p.circle(mowers[row]+9,177+row*54,4,0xe0f5e8);}
        }
        if(started && spawned<wave_size() && spawn_clock<2){double y=168+warning_row*54;p.line(394,y-7,402,y,0xffa657,3);p.line(402,y,394,y+7,0xffa657,3);}
        for(int i=0;i<40;i++){
            double x=56+i%8*44,y=168+i/8*54;
            if(stones[i]){p.circle(x,y+3,13,0x748c88);p.line(x-5,y,x+4,y-5,0xa6bab1,2);}
            const auto& a=plants[i];if(a.type<0)continue;double sway=std::sin(clock*2+i)*1.5;
            p.line(x,y+17,x+sway,y-4,leaf,3);p.line(x,y+12,x-10,y+7,leaf,4);
            if(a.type==0){for(int j=0;j<8;j++)p.circle(x+std::cos(j*.785)*10,y-5+std::sin(j*.785)*10,4,0xffd579);p.circle(x,y-5,7,0x875c36);}
            if(a.type==1 || a.type==3){unsigned c=a.type==3?0x8bdcff:leaf;p.circle(x+sway,y-5,11,c);p.line(x+5+sway,y-6,x+18+sway+(a.flash>0?2:0),y-6,c,9);p.circle(x+18+sway,y-6,3,0x102b29);p.circle(x-2+sway,y-10,2,0x102b29);}
            if(a.type==2){p.circle(x,y,16,0xc59c67);p.rect(x-15,y,30,13,0xc59c67);p.circle(x-5,y-4,2,0x30241b);p.circle(x+5,y-4,2,0x30241b);if(a.hp<240)p.line(x+6,y+3,x-3,y+10,0x875c36,2);}
            if(a.type==4){p.circle(x-6,y,10,0xf18683);p.circle(x+7,y+2,10,0xe65e72);p.line(x,y-7,x+4,y-16,leaf,3);}
            if(a.flash>0)p.line(x-12,y+22,x+12,y+22,0xffd579,2);
        }
        for(const auto& z:zombies){
            double y=168+z.row*54,bob=std::sin(z.walk)*2;unsigned c=z.flash>0?0xffffff:z.slow>0?0x8bdcff:0xb3bc85;
            p.line(z.x-5,y+9,z.x-8+std::sin(z.walk)*4,y+22,0x8c899f,5);p.line(z.x+5,y+9,z.x+8-std::sin(z.walk)*4,y+22,0x8c899f,5);
            p.rect(z.x-9,y-7+bob,18,20,z.type==2?0xb18bc6:0x917466);p.line(z.x-5,y,z.x-18,y-2,c,4);
            p.circle(z.x,y-15+bob,11,c);p.circle(z.x-4,y-17+bob,3,0xf7eddf);p.circle(z.x-5,y-17+bob,1.5,0x18221b);p.line(z.x-5,y-9+bob,z.x+3,y-8+bob,0x18221b,2);
            if(z.type==1){p.rect(z.x-11,y-31+bob,22,9,0xffa657);p.rect(z.x-7,y-37+bob,14,8,0xffa657);}
        }
        for(const auto& s:shots)p.circle(s.x,161+s.row*54,4,s.ice?0x8bdcff:leaf);
        for(const auto& s:suns){for(int j=0;j<6;j++)p.line(s.x+std::cos(j*1.047)*12,s.y+std::sin(j*1.047)*12,s.x+std::cos(j*1.047)*17,s.y+std::sin(j*1.047)*17,0xffd579,2);p.circle(s.x,s.y,10,0xffd579);}
        for(const auto& s:sparks)p.circle(s.x,s.y-s.age*24,std::max(1.,5-s.age*6),s.color);
        double x=34+cursor%8*44,y=141+cursor/8*54;
        p.line(x+2,y+2,x+41,y+2,shovel?0xffa657:leaf,2);p.line(x+2,y+2,x+2,y+50,shovel?0xffa657:leaf,2);
        p.text(24,432,11,shovel?"SHOVEL: click a plant to remove it":"Space: collect suns   X / right click: shovel",0x8dada1);
    }
};
}
