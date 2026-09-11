#include "../kar_physics.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace kalwer::games::kar_physics;
int main(){
 Motion m;m.reset(0);m.step(20);assert(m.speed==1539 && m.gear==1);
 for(int i=1;i<50;i++)m.step(20);
 assert(m.speed==76950 && m.kph()==68);
 m.step(20);m.step(20);m.step(20);assert(m.gear==2);
 m.speed=from_kph(400);assert(m.road_speed()==from_kph(250)+(from_kph(400)-from_kph(250))/3);
 m.reset(0);assert(m.boost() && m.stage==1 && m.boost_left==from_ms(2600));assert(!m.boost());m.step(20);assert(m.speed==2025);
 m.reset(0);m.refill(125);assert(m.boost() && m.stage==4 && m.boost_left==from_ms(4550));
 for(int i=0;i<300;i++)m.step(20);
 assert(m.stage==0 && m.nitro==0);
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
 // Position updates captured from the running reference, including bends,
 // segment boundaries, reverse grid placement, and finish-line wrapping.
 struct RoadSample {int distance,segment,offset,lap,lateral,curve,next,third,length,after_segment,after_offset,after_lap;};
 const RoadSample road_samples[]={
  {0,0,0,0,0,0,0,0,278,0,0,0},
  {-3584,277,512,0,185,0,0,0,278,274,0,0},
  {0,274,0,0,185,0,0,0,278,274,0,0},
  {49,277,980,0,185,0,0,0,278,0,5,1},
  {131,108,919,1,-92,0,0,0,278,109,26,1},
  {51,126,0,1,-1203,-11,-22,0,278,126,49,1},
  {40,126,1001,1,-1203,-11,-22,0,278,127,15,1},
  {40,127,15,1,-1203,-22,0,-5,278,127,52,1},
  {29,127,1019,1,-1203,-22,0,-5,278,128,22,1},
  {27,129,23,1,-1203,-5,-11,-17,278,129,50,1},
  {29,129,998,1,-1203,-5,-11,-17,278,130,2,1},
  {26,131,6,1,-1203,-17,-17,-22,278,131,30,1},
  {27,131,1022,1,-1203,-17,-17,-22,278,132,23,1},
  {27,134,10,1,-1203,-28,-28,-28,278,134,35,1},
  {27,134,1018,1,-1203,-28,-28,-28,278,135,19,1},
  {27,138,0,1,-1203,-34,-34,-34,278,138,24,1},
  {27,138,1023,1,-1203,-34,-34,-34,278,139,23,1},
  {72,181,75,1,-1023,11,22,28,278,181,150,1},
  {73,181,950,1,-1023,11,22,28,278,182,2,1},
  {77,182,2,1,-1023,22,28,34,278,182,85,1},
  {78,182,1012,1,-1004,22,28,34,278,183,72,1},
  {78,183,72,1,-1001,28,34,34,278,183,157,1},
  {84,183,954,1,-955,28,34,34,278,184,21,1},
  {76,184,21,1,-949,34,34,34,278,184,105,1},
  {80,184,1006,1,-860,34,34,34,278,185,70,1},
  {103,188,998,1,35,34,34,34,278,189,77,1},
  {87,189,77,1,65,34,34,28,278,189,163,1},
  {78,191,59,1,890,28,22,11,278,191,132,1},
  {75,191,993,1,1018,28,22,11,278,192,38,1},
  {71,192,38,1,1008,22,11,17,278,192,105,1},
  {77,192,984,1,1008,22,11,17,278,193,32,1},
  {73,193,32,1,1008,11,17,11,278,193,103,1},
  {74,193,1008,1,1008,11,17,11,278,194,56,1},
  {74,194,56,1,1008,17,11,5,278,194,126,1},
  {76,194,1020,1,1008,17,11,5,278,195,68,1},
  {78,196,41,1,1008,5,11,17,278,196,118,1},
  {84,196,991,1,1008,5,11,17,278,197,50,1},
 };
 for(auto s:road_samples){
  std::vector<int> curves(s.length);curves[s.segment]=s.curve;curves[(s.segment+1)%s.length]=s.next;curves[(s.segment+2)%s.length]=s.third;
  RoadPosition position{s.segment,s.offset,s.lap,s.lateral};position.advance(s.distance,curves);
  assert(position.segment==s.after_segment && position.offset==s.after_offset && position.lap==s.after_lap);
 }
 assert(RoadPosition::cosine(0)==4096 && RoadPosition::cosine(512)==0 && RoadPosition::cosine(1024)==-4096);
 assert(RoadPosition::cosine(-128)==3784 && RoadPosition::cosine(128)==3784);
 RoadPosition last{277,1000,0,0},first{0,20,1,0};assert(first.gap_to(last,278)==44 && last.gap_to(first,278)==-44);
 Steering steering;for(int i=0;i<60;i++)steering.controls(81,from_kph(160),30,-1,2,Steering::degrees(60));assert(steering.drift==-1);
 steering.controls(81,from_kph(99),30,-1,2,Steering::degrees(60));assert(steering.drift==0);
 steering.reset();for(int i=0;i<100;i++)steering.controls(81,from_kph(320),30,1,0,0);assert(steering.heading==Steering::degrees(20) && steering.drift==0);
 CollisionBody a,bcar;
 a.left_extent=a.right_extent=bcar.left_extent=bcar.right_extent=90;a.length=bcar.length=300;
 a.position={0,30,1,0};a.previous={277,1010,0,0};
 bcar.position=bcar.previous={0,100,1,0};
 assert(a.contact(bcar,278)==CollisionBody::Contact::side);
 a.position.lateral=200;assert(a.contact(bcar,278)==CollisionBody::Contact::none);
 // Fast crossing must still collide even when the final lateral boxes miss.
 a.previous.lateral=-200;assert(a.contact(bcar,278)==CollisionBody::Contact::side);
 a.height=101;assert(a.contact(bcar,278)==CollisionBody::Contact::none);
 a.height=100;assert(a.contact(bcar,278)==CollisionBody::Contact::side);
 a.wrecked=true;assert(a.contact(bcar,278)==CollisionBody::Contact::none);
 ImpactResponse impact;m.reset(0);m.speed=from_kph(200);before=m.speed;
 assert(impact.apply(ImpactResponse::Hit::front,from_kph(30),m,steering));
 assert(m.speed==before-(before>>2) && impact.cooldown==1638 && !impact.wrecked);
 m.stage=3;m.speed=before;impact.apply(ImpactResponse::Hit::front,from_kph(100),m,steering);assert(m.speed==before);
 impact.apply(ImpactResponse::Hit::from_left,0,m,steering);assert(steering.heading==11 && steering.camera==-17);
 impact.apply(ImpactResponse::Hit::from_right,0,m,steering);assert(steering.heading==-11 && steering.camera==17);
 m.stage=0;impact.apply(ImpactResponse::Hit::head_on,from_kph(80),m,steering);
 assert(impact.wrecked && impact.wreck_time==8192 && impact.height==1 && m.speed==(before>>2));
 before=m.speed;assert(!impact.apply(ImpactResponse::Hit::head_on,from_kph(100),m,steering));assert(m.speed==before);
 std::cout<<"Kar acceleration, nitro, captured road movement, collision geometry and impact rules passed.\n";
}
