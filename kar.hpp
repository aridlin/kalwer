#pragma once
#include "kar_physics.hpp"
#include "kar_course.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <random>
#include <string>

namespace kalwer::games {
struct Kar {
    using Motion=kar_physics::Motion;
    using Steering=kar_physics::Steering;
    using Road=kar_physics::RoadPosition;
    using Impact=kar_physics::ImpactResponse;
    using Body=kar_physics::CollisionBody;
    static constexpr int unit=4096,segments=int(kar_course::curves.size());
    enum class Phase {ready,countdown,racing,wreck,busted,finished};
    struct Racer {Motion motion;Road road,previous;Impact impact;int lane=0;bool police=false,traffic=false,oncoming=false;};
    std::array<Racer,12> racers{};
    struct Roadblock {Road road;bool active=false,broken=false;double age=0;};
    std::array<Roadblock,2> roadblocks{};
    Motion motion;Steering steering;Road road,previous;Impact impact;
    std::array<bool,256> held{};
    std::array<int,kar_course::pickups.size()> picked{};
    std::mt19937 random;
    Phase phase=Phase::ready;
    int vehicle=3,score=0,position=8,heat=0,knockdowns=0,arrests=0,drift_points=0;
    double elapsed=0,accumulator=0,phase_time=0,notice_time=0,heat_time=0,police_time=0,invulnerable=0,roadblock_time=0;
    int jump_height=0,jump_velocity=0;
    bool started=false,over=false,won=false,police_active=false;
    std::string notice;
    static int absolute(const Road& r){return (r.lap*segments+r.segment)*1024+r.offset;}
    static void place(Road& r,int distance,int lateral){
        int length=segments*1024;r.lap=distance/length;distance%=length;
        if(distance<0){distance+=length;--r.lap;}
        r.segment=distance/1024;r.offset=distance%1024;r.lateral=lateral;
    }
    void reset(unsigned seed){
        random.seed(seed);held.fill(false);picked.fill(-1);motion.reset(vehicle);steering.reset();impact={};
        road={segments-4,0,0,185};previous=road;phase=Phase::ready;
        score=0;position=8;heat=knockdowns=arrests=drift_points=0;
        elapsed=accumulator=phase_time=notice_time=heat_time=police_time=invulnerable=0;
        jump_height=jump_velocity=0;started=over=won=police_active=false;notice.clear();
        roadblocks={};roadblock_time=0;
        for(int i=0;i<int(racers.size());++i){
            auto& r=racers[i];r={};r.motion.reset(i%4);r.police=i==11;r.traffic=i>=7 && i<11;r.oncoming=r.traffic && i%2;
            r.lane=((i%3)-1)*520;
            place(r.road,absolute(road)+(i<7?(7-i)*512:10000+(i-7)*6000),r.lane);r.previous=r.road;
        }
    }
    void focus(bool focused){if(!focused){held.fill(false);accumulator=0;}}
    void say(std::string text,double seconds=1.3){notice=std::move(text);notice_time=seconds;}
    void key(int key,bool down=true){
        if(key<0 || key>=256)return;
        bool pressed=down && !held[key];held[key]=down;if(!pressed || over)return;
        if(phase==Phase::ready){
            if(key=='c'){vehicle=(vehicle+1)%4;motion.reset(vehicle);return;}
            if(key==13 || key==' '){started=true;phase=Phase::countdown;phase_time=3;return;}
        }
        if(phase!=Phase::racing)return;
        if((key==' ' || key==3 || key=='w') && motion.boost())say(motion.stage==4?"ADRENALINE":"NITRO "+std::to_string(motion.stage));
    }
    int bend_ahead()const {
        int sum=0,sign=kar_course::curves[road.segment];
        for(int n=0;n<segments;++n){int curve=kar_course::curves[(road.segment+n)%segments];if(curve*sign<=0)break;sum+=curve;}
        return sum;
    }
    Body body(const Road& current,const Road& prior,int height=0,bool wrecked=false)const {
        Body b;b.position=current;b.previous=prior;b.left_extent=b.right_extent=94;b.length=370;b.height=height;b.wrecked=wrecked;return b;
    }
    void respawn(){
        phase=Phase::racing;impact={};motion.speed=0;motion.stage=motion.boost_left=0;steering.reset();road.lateral=0;
        jump_height=jump_velocity=0;invulnerable=2;held.fill(false);say("GO!");
    }
    void hit(Impact::Hit type,int speed){
        if(invulnerable>0 || impact.cooldown>0)return;
        impact.apply(type,speed,motion,steering);
        if(impact.wrecked){phase=Phase::wreck;phase_time=2;jump_height=1;jump_velocity=impact.vertical_speed;say("CRASH!",2);}
        else say("CONTACT",.5);
        heat=std::min(100,heat+10);heat_time=0;
    }
    void step(){
        constexpr int ms=20,dt=81;constexpr double seconds=.02;
        if(notice_time>0)notice_time=std::max(0.,notice_time-seconds);
        if(invulnerable>0)invulnerable=std::max(0.,invulnerable-seconds);
        impact.cooldown=std::max(0,impact.cooldown-dt);
        if(phase==Phase::countdown){phase_time-=seconds;if(phase_time<=0){phase=Phase::racing;say("GO!");}return;}
        if(phase==Phase::busted){
            phase_time-=seconds;
            if(phase_time<=0){int fine=score/4;score-=fine;++arrests;heat=0;police_active=false;roadblocks={};respawn();say("FINE  $"+std::to_string(fine),2);}
            return;
        }
        elapsed+=seconds;
        previous=road;
        if(phase==Phase::wreck){
            phase_time-=seconds;motion.speed-=motion.speed*121/unit;
            road.lateral-=road.lateral*121/unit;
            jump_height=std::max(0,jump_height+(jump_velocity*dt/unit)/unit);jump_velocity-=dt*2200;
            if(phase_time<=0){if(police_active){phase=Phase::busted;phase_time=3.5;motion.speed=0;say("BUSTED",3.5);}else respawn();}
        }else{
            int input=(held[2] || held['d']?1:0)-(held[1] || held['a']?1:0);
            int curve=kar_course::curves[road.segment];
            bool brake=held[4] || held['s'];
            int prior_drift=steering.drift;
            steering.controls(dt,motion.speed,kar_physics::vehicles[vehicle].steering_degrees,input,curve,bend_ahead(),brake && input,jump_height>0);
            if(!prior_drift && steering.drift){drift_points=0;say("DRIFT");}
            if(prior_drift && !steering.drift && drift_points){score+=drift_points;say("DRIFT +"+std::to_string(drift_points));}
            if(steering.drift){drift_points+=8;motion.refill(drift_points%64==0?1:0);heat=std::min(100,heat+(drift_points%400==0?1:0));}
            int shoulder=std::abs(road.lateral)>kar_course::half_width-94?20:0;
            motion.step(ms,brake && !steering.drift,steering.drift!=0,motion.stage>=3?0:shoulder,curve,input,kar_course::grades[road.segment]);
            int travel=road.project(motion.speed,dt,steering.heading,steering.drift!=0);
            int rotation=road.advance(travel,kar_course::curves,!steering.drift);
            steering.track_rotation(rotation,motion.speed);
            if(std::abs(road.lateral)>kar_course::half_width+kar_course::shoulder-94){
                road.lateral=std::clamp(road.lateral,-1316,1316);steering.heading=steering.body=0;
                if(motion.stage<3)motion.speed-=motion.speed>>4;
            }
            if(jump_height>0){jump_height=std::max(0,jump_height+(jump_velocity*dt/unit)/unit);jump_velocity-=dt*2200;}
            if(previous.segment!=road.segment){
                int old_grade=kar_course::grades[previous.segment],grade=kar_course::grades[road.segment];
                if(!jump_height && old_grade-grade>22 && motion.kph()>140){jump_height=1;jump_velocity=60*unit;say("AIR TIME");score+=100;}
                for(size_t i=0;i<kar_course::pickups.size();++i){
                    const auto& p=kar_course::pickups[i];
                    if(p.segment!=road.segment || picked[i]==road.lap || (p.lap>=0 && p.lap!=road.lap) || std::abs(p.lateral-road.lateral)>220)continue;
                    picked[i]=road.lap;
                    if(p.kind==0){motion.refill(p.amount);say("NITRO +"+std::to_string(p.amount));}
                    else {score+=p.amount;say("$ +"+std::to_string(p.amount));}
                }
            }
        }
        for(int i=0;i<int(racers.size());++i){
            auto& r=racers[i];if(r.police && !police_active)continue;r.previous=r.road;
            if(r.impact.wrecked){r.impact.wreck_time-=dt;if(r.impact.wreck_time<=0){r.impact={};place(r.road,absolute(road)+16000,r.lane);}continue;}
            int gap=absolute(r.road)-absolute(road);
            if(r.traffic){r.motion.speed=kar_physics::from_kph(r.oncoming?80:65);}
            else{
                r.motion.step(ms);
                // Opponents use the reference's catch-up bands around the player.
                int percent=gap>4096?95:gap< -2048?125:105;
                int target=motion.speed*percent/100;
                target=std::clamp(target,kar_physics::from_kph(45),kar_physics::from_kph(kar_physics::vehicles[r.motion.vehicle].maximum));
                if(elapsed>8)r.motion.speed+=(target-r.motion.speed)/8;
                int aim=r.police?road.lateral:r.lane;
                int slide=r.police?6:3;r.road.lateral+=std::clamp(aim-r.road.lateral,-slide,slide);
            }
            int distance=Road::rounded_q12(std::int64_t(r.motion.road_speed())*dt)/40;
            r.road.advance(r.oncoming?-distance:distance,kar_course::curves,false);
            if(r.traffic && gap< -4000)place(r.road,absolute(road)+24000+int(random()%12000),r.lane);
            if(phase!=Phase::racing || invulnerable>0)continue;
            auto contact=body(road,previous,jump_height).contact(body(r.road,r.previous),segments);
            if(contact==Body::Contact::none)continue;
            if(motion.stage>=3){
                r.impact.apply(Impact::Hit::knockdown,0,r.motion,steering);++knockdowns;score+=r.police?1500:r.traffic?200:1000;heat=std::min(100,heat+20);say(r.police?"POLICE TAKEDOWN":"TAKEDOWN");
                if(r.police)police_active=false;
            }else{
                int closing=r.oncoming?motion.speed+r.motion.speed:std::abs(motion.speed-r.motion.speed);
                auto kind=contact==Body::Contact::side?(road.lateral<r.road.lateral?Impact::Hit::from_right:Impact::Hit::from_left):r.oncoming?Impact::Hit::head_on:contact==Body::Contact::front?Impact::Hit::front:Impact::Hit::rear;
                hit(kind,closing);
                if(contact==Body::Contact::side)road.lateral=r.road.lateral+(road.lateral<r.road.lateral?-190:190);
            }
        }
        position=1;for(int i=0;i<7;i++)if(absolute(racers[i].road)>absolute(road))++position;
        heat_time+=seconds;police_time+=seconds;
        if(heat_time>=2){heat_time=0;heat=std::max(0,heat-1);}
        if(heat>=70 && !police_active && police_time>=5){police_time=0;police_active=true;auto& cop=racers[11];cop.impact={};place(cop.road,absolute(road)-2500,-road.lateral);cop.motion.speed=motion.speed;say("POLICE PURSUIT",2);}
        if(heat<40)police_active=false;
        roadblock_time+=seconds;
        if(heat>=100 && roadblock_time>=5 && !roadblocks[0].active && !roadblocks[1].active){
            roadblock_time=0;
            for(int i=0;i<2;++i){auto& block=roadblocks[i];block={};block.active=true;place(block.road,absolute(road)+16000,i?-550:550);}
            say("ROADBLOCK AHEAD",2);
        }
        for(auto& block:roadblocks){
            if(!block.active)continue;
            int gap=absolute(block.road)-absolute(road);
            if(block.broken){block.age+=seconds;block.road.lateral+=(block.road.lateral<0?-1:1)*6;if(block.age>2)block.active=false;continue;}
            if(gap< -600){block.active=false;continue;}
            if(phase!=Phase::racing || invulnerable>0 || jump_height>100)continue;
            if(gap<370 && gap> -370 && std::abs(block.road.lateral-road.lateral)<450){
                if(motion.stage>=3){block.broken=true;score+=1500;++knockdowns;say("ROADBLOCK TAKEDOWN");}
                else{hit(Impact::Hit::knockdown,motion.speed);police_active=true;}
            }
        }
        if(road.lap>=4){phase=Phase::finished;over=true;won=position<=3;score+=(9-position)*1000;say(won?"PODIUM!":"RACE COMPLETE",10);held.fill(false);}
    }
    void advance(double dt){
        if(!started || over || dt<=0)return;
        accumulator+=std::min(dt,.1);
        while(accumulator+1e-9>=.02){accumulator-=.02;step();if(over)break;}
    }
    template<class P>struct Canvas {
        P& out;
        void rect(double x,double y,double w,double h,unsigned c){double x2=std::clamp(x+w,0.,240.),y2=std::clamp(y+h,0.,320.);x=std::clamp(x,0.,240.);y=std::clamp(y,0.,320.);if(x2>x && y2>y)out.rect(57+x*1.275,38+y*1.275,(x2-x)*1.275,(y2-y)*1.275,c);}
        void line(double x,double y,double a,double b,unsigned c,double w){if(x<0 || x>240 || a<0 || a>240)return;out.line(57+x*1.275,38+y*1.275,57+a*1.275,38+b*1.275,c,w*1.275);}
        void circle(double x,double y,double r,unsigned c){if(x-r<0 || x+r>240 || y-r<0 || y+r>320)return;out.circle(57+x*1.275,38+y*1.275,r*1.275,c);}
        void text(double x,double y,double size,const std::string& s,unsigned c){out.text(57+x*1.275,38+y*1.275,size*1.275,s,c);}
    };
    struct Projected {double x,y,half,depth;};
    struct View {std::array<double,62> z{},x{},elevation{};};
    View view()const {
        View v;double angle=0;
        for(int i=1;i<62;i++){
            int index=(road.segment+i-1)%segments;
            double step=i==1?1024-road.offset:1024;
            angle-=kar_course::curves[index]*6.283185307179586/2048.*step/1024.;
            v.z[i]=v.z[i-1]+step;
            v.x[i]=v.x[i-1]+std::sin(angle)*step;
            v.elevation[i]=v.elevation[i-1]+std::sin(kar_course::grades[index]*6.283185307179586/2048.)*step;
        }
        return v;
    }
    Projected project(const View& v,double distance,double lateral=0)const {
        int index=std::clamp(int((std::max(0.,distance)+road.offset)/1024),0,60);
        double part=std::clamp((distance-v.z[index])/(v.z[index+1]-v.z[index]),0.,1.);
        double x=v.x[index]+(v.x[index+1]-v.x[index])*part;
        double elevation=v.elevation[index]+(v.elevation[index+1]-v.elevation[index])*part;
        double heading=steering.camera*6.283185307179586/2048.;
        double xx=x+lateral-road.lateral-std::sin(heading)*distance;
        double factor=180/std::max(250.,distance+1250);
        return {120+xx*factor,142+(900-elevation*.35)*factor,1110*factor,factor};
    }
    template<class C>static void vehicle_art(C& p,double x,double y,double scale,unsigned color,int yaw=0,bool cop=false,bool front=false){
        double s=scale;auto r=[&](double a,double b,double w,double h,unsigned c){p.rect(x+a*s,y+b*s,w*s,h*s,c);};
        r(-32,-1,64,7,0x273335);r(-29,-28,9,30,0x172023);r(20,-28,9,30,0x172023);
        // Layered body panels, glass, lights and wheel arches at the phone scale.
        for(int row=0;row<30;row++){double t=row/29.,half=19+9*t,shift=yaw*(1-t)*2;r(-half+shift,-45+row,half*2,1.1,row<4?0xbdd5dc:color);}
        r(-27,-19,54,17,color);r(-28,-6,56,5,0x3e525c);r(-24,-42,48,3,0x9cb9ce);
        r(-18+yaw,-38,36,16,0x203b4c);r(-16+yaw,-37,14,12,0x7498a6);r(0+yaw,-37,16,12,0x526f7c);
        r(-21,-21,42,2,0xa4c6d1);r(-26,-17,11,6,front?0xfff8c1:0xd63d43);r(15,-17,11,6,front?0xfff8c1:0xd63d43);
        r(-25,-16,8,2,front?0xffffff:0xffa179);r(17,-16,8,2,front?0xffffff:0xffa179);
        r(-11,-11,22,6,0xcbd8d3);r(-8,-9,16,2,0x344755);r(-26,-4,52,2,0x8fa0a5);
        r(-33,-31,8,4,color);r(25,-31,8,4,color);
        if(cop){r(-25,-19,50,9,0xe5e9e4);r(-17,-43,17,4,0xff354d);r(0,-43,17,4,0x388bff);}
    }
    template<class C>void palm(C& p,double x,double y,double scale)const {
        if(scale<.03 || x<12 || x>228)return;
        double h=std::min(95.,scale*1400),top=y-h;
        p.line(x,y,x+h*.12,top,0x765c3d,std::max(1.,scale*13));
        for(int i=-2;i<=2;i++){double end=x+i*h*.25;p.line(x+h*.12,top,end,top+std::abs(i)*h*.09,0x337b45,std::max(1.,scale*19));}
    }
    template<class P>void draw(P& output)const {
        Canvas<P> p{output};const auto projected=view();
        p.rect(0,0,240,320,0x82cce6);p.rect(0,113,240,40,0x5bbdc5);p.rect(0,146,240,174,0xd4ca87);
        p.circle(191,72,15,0xfff2b5);
        for(int i=0;i<20;i++){double x=i*14;double h=5+(i*13)%16;p.rect(x,137-h,14,h,0x5a967c);}
        // Road strips are projected from course geometry, with the same curve
        // coordinates used by physics and roadside objects.
        for(int strip=180;strip>=-2;--strip){
            double far_distance=(strip+1)*256.,near_distance=strip*256.;
            auto distant=project(projected,far_distance),close_point=project(projected,near_distance);
            if(close_point.y<=distant.y)continue;
            for(int yy=std::max(0,int(std::ceil(distant.y)));yy<std::min(320,int(std::ceil(close_point.y)));++yy){
                double mix=std::clamp((yy-distant.y)/(close_point.y-distant.y),0.,1.);
                double center=distant.x+(close_point.x-distant.x)*mix,half=distant.half+(close_point.half-distant.half)*mix,depth=distant.depth+(close_point.depth-distant.depth)*mix;
                bool stripe=(int((absolute(road)+near_distance)/700)&1)!=0;
                p.rect(0,yy,240,1,stripe?0xc5c485:0xd4ca87);
                p.rect(center-half-5,yy,half*2+10,1,stripe?0xe7e5d1:0xc75a4a);
                p.rect(center-half,yy,half*2,1,stripe?0x70767a:0x747a7d);
                if(stripe)for(int lane:{-1,0,1})p.rect(center+lane*half*.5-depth*6,yy,std::max(1.,depth*12),1,0xe4e7db);
            }
        }
        for(int i=28;i>=1;--i){
            double distance=i*1400-std::fmod(double(absolute(road)),1400.);auto q=project(projected,distance);
            if(i%3==0){palm(p,q.x-q.half-q.depth*550,q.y,q.depth);palm(p,q.x+q.half+q.depth*650,q.y,q.depth);}
            if(i%4==0){double x=q.x+q.half+q.depth*500,w=q.depth*1000,h=q.depth*1800;p.rect(x,q.y-h,w,h,0xeee1bf);p.rect(x,q.y-h,w,3*q.depth*30,0xbb7658);for(int j=0;j<3;j++)p.rect(x+w*(j+.2)/3,q.y-h*.7,w*.12,h*.25,0x68909b);}
        }
        for(size_t i=0;i<kar_course::pickups.size();++i){const auto& pick=kar_course::pickups[i];if(picked[i]==road.lap)continue;Road at{pick.segment,0,road.lap,pick.lateral};int distance=at.gap_to(road,segments);if(distance<100 || distance>16000)continue;auto q=project(projected,distance,pick.lateral);if(q.x<8 || q.x>225)continue;double size=std::clamp(q.depth*110,4.,16.);p.text(q.x-size*.4,q.y,size,pick.kind==0?"N":"$",pick.kind==0?0x55eeff:0xffeb61);}
        for(const auto& block:roadblocks){if(!block.active)continue;int gap=absolute(block.road)-absolute(road);if(gap<0 || gap>20000)continue;auto q=project(projected,gap,block.road.lateral);double w=q.depth*720,h=q.depth*240;p.rect(q.x-w/2,q.y-h,w,h,0xe0e4df);for(int i=0;i<5;i++)p.rect(q.x-w/2+w*i/5,q.y-h,w/10,h,0xe76e3f);p.circle(q.x-w*.3,q.y-h*1.25,std::max(1.,h*.12),0xffae34);p.circle(q.x+w*.3,q.y-h*1.25,std::max(1.,h*.12),0xffae34);}
        std::array<int,12> order{};for(int i=0;i<12;i++)order[i]=i;
        std::sort(order.begin(),order.end(),[&](int a,int b){return racers[a].road.gap_to(road,segments)>racers[b].road.gap_to(road,segments);});
        constexpr unsigned colors[]={0x5795bd,0xd75543,0xd2b45a,0x778994,0x8b6faf,0xe8e9df,0x628966};
        for(int i:order){const auto& r=racers[i];if(r.police && !police_active)continue;int gap=r.road.gap_to(road,segments);if(gap<0 || gap>16000 || r.impact.wrecked)continue;auto q=project(projected,gap,r.road.lateral);if(q.x<20 || q.x>220)continue;vehicle_art(p,q.x,q.y,std::min(1.1,q.depth*9),colors[i%7],0,r.police,r.oncoming);}
        if(motion.stage>0){p.line(101,286,97,308,0x91ecff,5);p.line(139,286,143,308,0x91ecff,5);}
        if(steering.drift){p.line(91,283,80-steering.drift*8,312,0x596264,2);p.line(145,283,146-steering.drift*8,312,0x596264,2);}
        if(invulnerable<=0 || int(elapsed*12)%2==0)vehicle_art(p,120,282-std::min(70,jump_height),1,0x689fbd,(steering.body-steering.camera)/28);
        p.text(5,19,21,std::to_string(position),0xffe560);p.text(24,16,11,"/8",0x233e49);
        p.rect(96,2,48,19,0x256477);p.text(104,16,12,std::to_string(std::clamp(road.lap,1,3))+" / 3",0xe9f4d9);
        p.text(166,14,10,std::to_string(score)+" $",0xffe560);p.text(181,34,9,"WANTED",heat>=70?0xffd34c:0x213d47);
        for(int i=0;i<5;i++)p.rect(168+i*2,26,1.5,8,heat>=20*(i+1)?0xec623c:0x456473);
        p.rect(48,305,144,12,0x1c3c4a);p.rect(50,307,140.*motion.nitro/(150*unit),8,motion.stage==4?0xffd85e:0x4dbbea);
        p.text(183,304,20,std::to_string(motion.kph()),0xffe363);p.text(188,318,8,"KM/H",0xe4f4e9);
        if(notice_time>0 && phase!=Phase::busted)p.text(36,87,14,notice,0xffec8c);
        if(phase==Phase::ready){p.rect(16,83,208,78,0x174757);p.text(78,104,22,"KAR",0xe7f6e8);p.text(30,124,11,"BAHAMAS  /  THREE LAPS",0x9ddcf1);p.text(29,141,10,"C: vehicle  "+std::to_string(kar_physics::vehicles[vehicle].maximum)+" km/h",0xe7f6e8);p.text(49,154,10,"ENTER / SPACE TO RACE",0xffdc71);}
        if(phase==Phase::countdown)p.text(105,118,42,std::to_string(std::max(1,int(std::ceil(phase_time)))),0xffe166);
        if(phase==Phase::busted){
            p.rect(16,71,208,172,0x163947);p.rect(16,71,104,7,0xec4e58);p.rect(120,71,104,7,0x4e9bee);
            p.circle(120,126,22,0xc69470);p.rect(91,100,58,12,0x213044);p.rect(99,90,42,16,0x293d58);p.rect(115,94,10,8,0xecc56c);
            p.rect(97,118,20,6,0x172731);p.rect(123,118,20,6,0x172731);p.line(117,120,123,120,0x172731,2);
            p.rect(88,147,64,49,0x2d4b6c);p.rect(113,146,14,49,0x233449);p.circle(102,160,5,0xf2ca6a);
            p.text(70,214,21,"BUSTED",0xffcf70);p.text(44,232,11,"FINE: 25% OF RACE CASH",0xdceadf);
        }
        if(over){p.rect(19,94,202,78,0x164655);p.text(39,116,19,won?"PODIUM FINISH!":"RACE COMPLETE",0xffdf77);p.text(47,140,13,"POSITION "+std::to_string(position)+" / 8",0xe8f3df);p.text(45,159,11,"R TO RACE AGAIN",0x92d7ef);}
        output.text(15,467,10,"Arrows/WASD: drive  Space/Up: nitro  Down+steer: drift",0x8dada1);
    }
};
}
