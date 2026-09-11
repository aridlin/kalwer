#include "../games.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
struct Paint {
    int calls=0;
    void rect(double x,double y,double w,double h,unsigned){assert(std::isfinite(x+y+w+h) && w>=0 && h>=0);++calls;}
    void circle(double x,double y,double r,unsigned){assert(std::isfinite(x+y+r));++calls;}
    void line(double x,double y,double a,double b,unsigned,double w){assert(std::isfinite(x+y+a+b+w));++calls;}
    void text(double x,double y,double size,const std::string&,unsigned){assert(std::isfinite(x+y+size));++calls;}
};
int main(){
 using namespace kalwer;using namespace kalwer::games;
 auto root=std::filesystem::temp_directory_path()/"kalwer-kar-test-data";std::filesystem::create_directories(root);wallet.path=root/"wallet";wallet.balance=100000;wallet.owned=wallet.equipped=0;
 assert(wallet.purchase(7,12000));Game game(Kind::kar,42);Paint painter;
 game.focus(true);game.key(13);game.release(13);assert(game.started);
 for(int i=0;i<600;i++)game.tick(.02);
 assert(game.kar.motion.speed>0);
 auto before=game.kar.road;game.key(2);game.focus(false);game.tick(60);game.key(1);
 assert(game.kar.road.segment==before.segment && game.kar.road.offset==before.offset && game.kar.road.lateral==before.lateral && !game.kar.held[2]);
 game.focus(true);game.kar.phase=Kar::Phase::busted;game.kar.phase_time=3.5;game.kar.score=4000;
 game.focus(false);game.tick(100);assert(game.kar.phase_time==3.5 && game.kar.score==4000);
 game.focus(true);for(int i=0;i<176;i++)game.tick(.02);assert(game.kar.arrests==1 && game.kar.score==3000 && game.kar.phase==Kar::Phase::racing);
 // A roadblock catches an ordinary car; the third nitro stage breaks through.
 for(bool boosted:{false,true}){
  Kar race;race.reset(5);race.started=true;race.phase=Kar::Phase::racing;
  for(auto& r:race.racers)race.place(r.road,100000,900);
  auto& block=race.roadblocks[0];block.active=true;block.road=race.road;
  race.motion.speed=kar_physics::from_kph(180);
  if(boosted){race.motion.stage=3;race.motion.boost_left=4096;}
  race.step();assert(boosted?block.broken:race.phase==Kar::Phase::wreck);
 }
 // A finish pays once; restarting does not repeat the previous payout.
 game.kar.road.lap=4;for(auto& r:game.kar.racers)r.road.lap=0;
 game.tick(.02);assert(game.over && game.won);auto balance=wallet.balance;
 game.tick(.02);assert(wallet.balance>balance);balance=wallet.balance;game.tick(.02);assert(wallet.balance==balance);
 game.key('r');assert(!game.over && !game.started);game.tick(.02);assert(wallet.balance==balance);
 game.kar.phase=Kar::Phase::racing;game.kar.started=true;
 auto start=std::chrono::steady_clock::now();
 for(int i=0;i<2000;i++){game.kar.key(i%2?1:2);game.kar.advance(.02);game.kar.draw(painter);game.kar.key(1,false);game.kar.key(2,false);}
 assert(painter.calls>1000);
 std::cout<<"Kar focus, held-input release, police fine, finish reward, restart and 2000 simulation/render frames passed in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s.\n";
 std::filesystem::remove_all(root);
}
