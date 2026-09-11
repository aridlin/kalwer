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
 Steering steering;for(int i=0;i<60;i++)steering.controls(81,from_kph(160),30,-1,2,Steering::degrees(60));assert(steering.drift==-1);
 steering.controls(81,from_kph(99),30,-1,2,Steering::degrees(60));assert(steering.drift==0);
 steering.reset();for(int i=0;i<100;i++)steering.controls(81,from_kph(320),30,1,0,0);assert(steering.heading==Steering::degrees(20) && steering.drift==0);
 std::cout<<"Reference Q12 acceleration, gear shifts, high-speed travel and nitro timing passed.\n";
}
