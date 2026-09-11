#include "../kar_physics.hpp"
#include <cassert>
#include <iostream>
using namespace kalwer::games::kar_physics;
int main(){
 Motion m;m.reset(0);m.step(20);assert(m.speed==1539 && m.gear==1);
 for(int i=1;i<50;i++)m.step(20);assert(m.speed==76950 && m.kph()==68);
 m.step(20);m.step(20);m.step(20);assert(m.gear==2);
 m.speed=from_kph(400);assert(m.road_speed()==from_kph(250)+(from_kph(400)-from_kph(250))/3);
 m.reset(0);assert(m.boost() && m.stage==1 && m.boost_left==from_ms(2600));assert(!m.boost());m.step(20);assert(m.speed==2025);
 m.reset(0);m.refill(125);assert(m.boost() && m.stage==4 && m.boost_left==from_ms(4550));
 for(int i=0;i<300;i++)m.step(20);assert(m.stage==0 && m.nitro==0);
 int before=m.speed;m.step(20,true);assert(m.speed<before);
 // Captured straight-road ticks from the running 240x320 Java reference.
 struct Sample{int dt,before,gear,after;};
 const Sample observed[]={
  {258,0,1,3612},
  {81,3612,1,4746},
  {86,10416,1,11620},
  {77,17360,1,18438},
  {81,79240,1,79644},
  {81,79644,2,79887},
  {86,81831,2,82089},
  {77,86265,2,86496},
  {81,113694,2,113777},
  {81,113777,3,114020},
  {86,114020,3,114278},
  {77,117938,3,118169},
  {81,159209,3,159288},
  {81,159288,4,159531},
  {86,160503,4,160761},
  {90,162948,4,163218},
  {77,163218,4,163449},
  {77,204606,4,204800},
  {81,204800,5,205043},
  {86,205772,5,206030},
  {77,206546,5,206777},
  {86,250208,5,250311},
  {81,250311,6,250554},
  {86,251040,6,251298},
 };
 for(auto sample:observed){Motion replay;replay.reset(3);replay.speed=sample.before;replay.gear=sample.gear;replay.step((sample.dt*1000+4095)/4096);assert(replay.speed==sample.after);}
 Steering steering;for(int i=0;i<60;i++)steering.controls(81,from_kph(160),30,-1,2,Steering::degrees(60));assert(steering.drift==-1);
 steering.controls(81,from_kph(99),30,-1,2,Steering::degrees(60));assert(steering.drift==0);
 steering.reset();for(int i=0;i<100;i++)steering.controls(81,from_kph(320),30,1,0,0);assert(steering.heading==Steering::degrees(20) && steering.drift==0);
 std::cout<<"Reference Q12 acceleration, gear shifts, high-speed travel and nitro timing passed.\n";
}
