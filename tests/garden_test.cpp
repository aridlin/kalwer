#include "../games.hpp"
#include <cassert>
#include <iostream>
using namespace kalwer::games;
void advance(Garden& g,double t){for(int i=0;i<int(t*60);i++)g.advance(1./60);}
int main(){
    Garden g;g.reset(9);g.selected=0;g.place(0);assert(g.sun==150 && g.plants[0].type==0 && g.started);
    g.place(1);assert(g.plants[1].type==-1);advance(g,7);assert(!g.suns.empty());int before=g.sun;g.key(' ');assert(g.suns.empty() && g.sun>before);
    g.shovel=true;g.place(0);assert(g.plants[0].type==-1);g.shovel=false;g.sun=0;g.place(0);assert(g.plants[0].type==-1);
    g.reset(1,true);assert(g.waves()==6 && g.stones[10]);g.place(10);assert(g.plants[10].type==-1);
    g.reset(2);g.started=true;g.spawn_clock=999;g.zombies={{0,0,34,80,0,0,0}};advance(g,.2);assert(g.mowers[0]>=0);advance(g,1);assert(g.zombies.empty());
    g.zombies={{0,0,13,80,0,0,0}};advance(g,1);assert(g.over && !g.won);
    g.reset(3);g.sun=1000;g.selected=4;g.place(16);g.zombies={{2,1,60,180,0,0,0},{3,0,65,80,0,0,0}};advance(g,1.2);assert(g.zombies.empty() && g.kills==2 && g.plants[16].type==-1);
    g.reset(2);g.started=true;g.spawn_clock=999;g.zombies={{0,1,200,180,0,0,0}};g.shots={{0,190,true}};advance(g,.05);assert(g.zombies[0].slow>0 && g.zombies[0].hp<180);
    for(bool night:{false,true}){
        g.reset(9,night);g.selected=0;g.place(0);
        bool second_sun=false;
        for(int i=0;i<60*300 && !g.over;i++){
            g.key(' ');
            if(!second_sun && g.cooldown[0]<=0 && g.sun>=150){g.selected=0;g.place(8);second_sun=true;}
            int priority=g.warning_row;double nearest=10000;
            for(auto z:g.zombies)if(z.x<nearest && g.plants[z.row*8+1].type<0){priority=z.row;nearest=z.x;}
            if(g.cooldown[1]<=0 && g.sun>=100){g.selected=1;if(g.plants[priority*8+1].type<0)g.place(priority*8+1);else for(int row=0;row<5;row++)if(g.plants[row*8+2].type<0 && !g.stones[row*8+2]){g.place(row*8+2);break;}}
            g.advance(1./60);
        }
        assert(g.over && g.won);std::cout<<(night?"Moon":"Meadow")<<" defended in "<<g.clock<<"s\n";
    }
    Game paused(Kind::garden,1);paused.focused=true;paused.key(' ');paused.tick(.05);paused.focused=false;double clock=paused.garden.clock;for(int i=0;i<100;i++){paused.tick(.05);paused.key('1');paused.pointer(60,165,1);}assert(paused.garden.clock==clock && paused.garden.sun==200);
    std::cout<<"Garden economy, cooldowns, sun collection, shovel, mowers, bomb, frost, waves and focus pause passed.\n";
}
