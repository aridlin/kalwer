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
 Kar projection;projection.reset(42);Kar::View flat;
 for(int i=0;i<62;++i)flat.z[i]=i*1024;
 assert(std::abs(Kar::vehicle_scale(projection.project(flat,450))-.5)<1e-9);
 assert(std::abs(Kar::vehicle_scale(projection.project(flat,1350))-.25)<1e-9);
 assert(std::abs(projection.project(flat,450).y-(200.+204.*200/900))<1e-9);
 for(int i=0;i<7;++i){assert(projection.racers[i].lane==(i%2?1:-1)*(kar_course::half_width/6));assert(projection.racers[i].road.gap_to(projection.road,Kar::segments)==(7-i)*512);assert(projection.racers[i].motion.vehicle==projection.vehicle);}
 // A clean straight run can overtake rivals; they must not continually target
 // a faster class as soon as the player catches them.
 projection.key(13);projection.invulnerable=1000;
 for(int i=0;i<3000;++i){projection.road.lateral=185;projection.steering.heading=0;projection.advance(.02);}
 assert(projection.position<8);
 for(int i=0;i<7;++i)for(int j=i+1;j<7;++j)if(projection.racers[i].lane==projection.racers[j].lane)assert(std::abs(projection.racers[i].road.gap_to(projection.racers[j].road,Kar::segments))>370);
 auto root=std::filesystem::temp_directory_path()/"kalwer-kar-test-data";std::filesystem::create_directories(root);wallet.path=root/"wallet";wallet.balance=100000;wallet.owned=wallet.equipped=0;
 assert(wallet.purchase(7,12000));Game game(Kind::kar,42);Paint painter;
 game.focus(true);game.key(13);game.release(13);assert(!game.started);
 game.tick(10);assert(!game.started && game.kar.elapsed==0);
 // Physics tests use an in-memory art fixture; production first-use loading
 // must block race progress until its worker supplies verified artwork.
 game.kar.artwork=std::make_shared<kar_pixels::ArtRequest>();
 game.kar.artwork->result=std::make_shared<kar_pixels::Art>();
 game.kar.artwork->ready.store(true);
 game.key(13);game.release(13);assert(game.started);
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
 // Sustained input-only driving: all three seeded races remain completable,
 // and the default car can win without mutating its physics or opponents.
 int driving_wins=0;
 for(int seed:{1,42,123}){
  Kar k;k.reset(seed);k.key(13);int crashes=0;auto phase=k.phase;double target=0;
  for(int tick=0;tick<30000 && !k.over;++tick){
   if(k.phase==Kar::Phase::racing){
    double best=-1e9;
    for(int lane:{-650,0,650}){
     int free=16000;
     for(const auto& r:k.racers){
      int gap=r.road.gap_to(k.road,k.segments);
      if((!r.police || k.police_active) && gap> -500 && gap<16000 && std::abs(r.road.lateral-lane)<240)free=std::min(free,std::max(0,gap));
     }
     double merit=free-std::abs(lane-k.road.lateral)*2+(lane==0?1000:0);
     if(merit>best){best=merit;target=lane;}
    }
    double control=target-k.road.lateral-k.steering.heading*9;
    k.key(1,control< -30);k.key(2,control>30);
    if(tick%350==0){k.key(' ');k.key(' ',false);}
   }
   k.advance(.02);if(k.phase==Kar::Phase::wreck && phase!=k.phase)++crashes;phase=k.phase;
  }
  assert(k.over && crashes<12);driving_wins+=k.won;
 }
 assert(driving_wins>0);
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
