#pragma once
#include "kar_physics.hpp"
#include "kar_course.hpp"
#include "kar_pixels.hpp"
#include "kar_art.hpp"
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
    bool nitro_reserve=false,quick_recovery=false;
    bool started=false,over=false,won=false,police_active=false;
    std::string notice;
    std::shared_ptr<kar_pixels::ArtRequest> artwork;
    void load_art(const std::filesystem::path& path){if(!artwork || (artwork->ready.load(std::memory_order_acquire) && !artwork->get()))artwork=kar_pixels::ArtRequest::start(path);}
    static int absolute(const Road& r){return (r.lap*segments+r.segment)*1024+r.offset;}
    static void place(Road& r,int distance,int lateral){
        int length=segments*1024;r.lap=distance/length;distance%=length;
        if(distance<0){distance+=length;--r.lap;}
        r.segment=distance/1024;r.offset=distance%1024;r.lateral=lateral;
    }
    void reset(unsigned seed){
        random.seed(seed);held.fill(false);picked.fill(-1);motion.reset(vehicle);if(nitro_reserve)motion.refill(50);steering.reset();impact={};
        road={segments-4,0,0,185};previous=road;phase=Phase::ready;
        score=0;position=8;heat=knockdowns=arrests=drift_points=0;
        elapsed=accumulator=phase_time=notice_time=heat_time=police_time=invulnerable=0;
        jump_height=jump_velocity=0;started=over=won=police_active=false;notice.clear();
        roadblocks={};roadblock_time=0;
        for(int i=0;i<int(racers.size());++i){
            auto& r=racers[i];r={};r.motion.reset(i<7?vehicle:i%4);r.police=i==11;r.traffic=i>=7 && i<11;r.oncoming=r.traffic && i%2;
            // Reference starting grid: alternating sides at road half-width/6,
            // staggered by 512 longitudinal units (not three broad columns).
            r.lane=i<7?(i%2?1:-1)*(kar_course::half_width/6):((i%2?1:-1)*720);
            place(r.road,absolute(road)+(i<7?(7-i)*512:10000+(i-7)*6000),r.lane);r.previous=r.road;
        }
    }
    void focus(bool focused){if(!focused){held.fill(false);accumulator=0;}}
    void say(std::string text,double seconds=1.3){notice=std::move(text);notice_time=seconds;}
    void key(int key,bool down=true){
        if(artwork && !artwork->get())return;
        if(key<0 || key>=256)return;
        bool pressed=down && !held[key];held[key]=down;if(!pressed || over)return;
        if(phase==Phase::ready){
            if(key=='c'){vehicle=(vehicle+1)%4;motion.reset(vehicle);if(nitro_reserve)motion.refill(50);for(int i=0;i<7;++i)racers[i].motion.reset(vehicle);return;}
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
        if(impact.wrecked){phase=Phase::wreck;phase_time=quick_recovery?1:2;jump_height=1;jump_velocity=impact.vertical_speed;say("CRASH!",2);}
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
                // Match the player's vehicle class and the reference rival
                // pace caps. Previously every default-car opponent had a
                // faster class and targeted 105% speed whenever approached.
                constexpr int pace[]={93,93,93,93,95,95,95};
                int maximum=kar_physics::vehicles[r.motion.vehicle].maximum;
                if(!r.police)maximum=maximum*pace[i%7]/100;
                target=std::clamp(target,kar_physics::from_kph(45),kar_physics::from_kph(maximum));
                if(elapsed>8)r.motion.speed+=(target-r.motion.speed)/8;
                int aim=r.police?road.lateral:r.lane;
                int slide=r.police?6:3;r.road.lateral+=std::clamp(aim-r.road.lateral,-slide,slide);
            }
            if(!r.traffic && !r.police){
                // Rivals must follow the car ahead instead of converging onto
                // the same catch-up position and drawing through one another.
                int nearest=1800,leader_speed=r.motion.speed;
                auto consider=[&](const Road& ahead,int speed){int gap=ahead.gap_to(r.road,segments);if(gap>0 && gap<nearest && std::abs(ahead.lateral-r.road.lateral)<240){nearest=gap;leader_speed=speed;}};
                for(int j=0;j<int(racers.size());++j){const auto& other=racers[j];if(j!=i && !other.oncoming && !other.impact.wrecked && (!other.police || police_active))consider(other.road,other.motion.speed);}
                if(phase==Phase::racing)consider(road,motion.speed);
                if(nearest<1800){
                    int following=std::max(0,leader_speed+kar_physics::from_kph((nearest-750)*36/1000));
                    if(r.motion.speed>following)r.motion.speed-=std::min(r.motion.speed-following,kar_physics::from_kph(2));
                    // Never integrate through the preceding car during a hard
                    // stop, even when ordinary braking cannot recover the gap.
                    int clearance_speed=std::max(0,nearest-500)*40*unit/dt;
                    r.motion.speed=std::min(r.motion.speed,clearance_speed);
                }
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
        if(artwork && !artwork->get())return;
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
        // Reference camera: 200 px focal length, 450-unit sprite scale,
        // 204-unit camera height, and a 200 px horizon. Sprite scale and road
        // projection must share this denominator or cars engulf whole lanes.
        double factor=200/std::max(250.,distance+450);
        return {120+xx*factor,200+(204-elevation)*factor,kar_course::half_width*factor,factor};
    }
    static double vehicle_scale(const Projected& point){return std::min(1.1,point.depth*450/200);}
    template<class C>static void vehicle_art(C& p,double x,double y,double scale,unsigned color,int yaw=0,bool cop=false,bool front=false){
        double s=scale;auto r=[&](double a,double b,double w,double h,unsigned c){p.rect(x+a*s,y+b*s,w*s,h*s,c);};
        auto shade=[&](unsigned c,int percent){unsigned out=0;for(int shift:{0,8,16})out|=unsigned(std::clamp(int((c>>shift)&255)*percent/100,0,255))<<shift;return out;};
        // Wide low sports-car silhouette: separate roof, sloping glass, rear
        // deck, wheel arches and diffuser. Yaw shifts the upper body in depth.
        yaw=std::clamp(yaw,-5,5);
        for(int row=0;row<7;row++){double half=36*std::sqrt(std::max(0.,1-std::pow((row-3)/4.,2)));r(-half,row-2,half*2,1.1,0x39413f);}
        r(-32,-22,8,25,0x142027);r(24,-22,8,25,0x142027);
        for(int row=0;row<35;row++){
            double half=row<8?17+row*.85:row<20?24+(row-8)*.7:32-(row-20)*.12;
            double shift=yaw*(1-row/35.)*1.8;
            r(-half+shift,-39+row,half*2,1.1,shade(color,row<7?120:row<20?108:row<25?88:68));
        }
        for(int row=0;row<12;row++){double half=17+row*.45,shift=yaw*(1-row/16.);r(-half+shift,-32+row,half*2,1.1,row<3?0x90bccd:row<7?0x436d83:0x253f51);}
        r(-16+yaw,-35,32,2,shade(color,145));
        for(int row=0;row<5;row++)r(-25+row,-19+row,50-row*2,1.1,shade(color,125-row*7));
        r(-32,-19,8,7,shade(color,72));r(24,-19,8,7,shade(color,65));
        r(-29,-15,17,5,front?0xeaf7ff:0xaa263b);r(12,-15,17,5,front?0xeaf7ff:0xaa263b);
        r(-28,-14,15,2,front?0xffffff:0xff7370);r(13,-14,15,2,front?0xffffff:0xff7370);
        r(-29,-6,58,4,0x26343d);r(-21,-5,42,4,0x101c24);
        for(int i=-2;i<=2;i++)r(i*6,-5,1,5,0x4c5c63);
        r(-9,-12,18,5,0xc6d0c9);r(-6,-10,12,1,0x344755);
        r(-27,-4,5,2,0xa3b4bd);r(22,-4,5,2,0xa3b4bd);
        r(-34,-24,6,3,shade(color,110));r(28,-24,6,3,shade(color,110));
        if(!front && !cop){r(-27,-20,3,5,0x35434e);r(24,-20,3,5,0x35434e);r(-31,-22,62,3,shade(color,132));}
        if(cop){r(-27,-15,54,8,0xe5e9e4);r(-16+yaw,-39,16,4,0xff354d);r(yaw,-39,16,4,0x388bff);}

    }
    template<class C>void palm(C& p,double x,double y,double scale)const {
        if(scale<.03 || x<12 || x>228)return;
        double h=std::min(95.,scale*1400),top=y-h;
        p.line(x,y,x+h*.12,top,0x765c3d,std::max(1.,scale*13));
        for(int i=-2;i<=2;i++){double end=x+i*h*.25;p.line(x+h*.12,top,end,top+std::abs(i)*h*.09,0x337b45,std::max(1.,scale*19));}
    }
    void draw_scene(kar_pixels::Surface& p,const kar_pixels::Art& art,const View& view)const{
        for(int ahead=12;ahead>=0;--ahead){
            int segment=(road.segment+ahead)%segments;
            auto found=art.scene_segments.find(segment);if(found==art.scene_segments.end())continue;
            for(size_t index:found->second){const auto& primitive=art.scene[index];std::array<Projected,3> points{};
                int count=(primitive.kind==0 || primitive.kind==3)?3:2;
                for(int vertex=0;vertex<count;++vertex){int at=vertex*3;
                    int distance=primitive.vertices[at+1]-(road.segment*1024+road.offset);
                    if(distance< -segments*512)distance+=segments*1024;
                    if(distance>segments*512)distance-=segments*1024;
                    points[vertex]=project(view,distance,primitive.vertices[at]);
                    points[vertex].y-=primitive.vertices[at+2]*points[vertex].depth;
                }
                if(primitive.kind==3){
                    auto sprite=art.get(primitive.bank,primitive.frame);if(!sprite)continue;
                    std::array<double,6> uv{},xy{};int axis=0;
                    if(primitive.vertices[0]==primitive.vertices[3] && primitive.vertices[3]==primitive.vertices[6])axis=1;
                    int vertical=2;
                    if(primitive.vertices[2]==primitive.vertices[5] && primitive.vertices[5]==primitive.vertices[8])vertical=1;
                    double u0=std::min({primitive.vertices[axis],primitive.vertices[axis+3],primitive.vertices[axis+6]}),u1=std::max({primitive.vertices[axis],primitive.vertices[axis+3],primitive.vertices[axis+6]});
                    double v0=std::min({primitive.vertices[vertical],primitive.vertices[vertical+3],primitive.vertices[vertical+6]}),v1=std::max({primitive.vertices[vertical],primitive.vertices[vertical+3],primitive.vertices[vertical+6]});
                    for(int vertex=0;vertex<3;++vertex){xy[vertex*2]=points[vertex].x;xy[vertex*2+1]=points[vertex].y;uv[vertex*2]=(primitive.vertices[vertex*3+axis]-u0)/std::max(1.,u1-u0);uv[vertex*2+1]=1-(primitive.vertices[vertex*3+vertical]-v0)/std::max(1.,v1-v0);}
                    p.textured_triangle(xy,uv,sprite->pixels.data(),sprite->width,sprite->height);
                }else if(primitive.kind==0)p.triangle(points[0].x,points[0].y,points[1].x,points[1].y,points[2].x,points[2].y,primitive.color);
                else{
                    int x=int(std::floor(std::min(points[0].x,points[1].x))),y=int(std::floor(std::min(points[0].y,points[1].y)));
                    int w=int(std::ceil(std::abs(points[1].x-points[0].x))),h=int(std::ceil(std::abs(points[1].y-points[0].y)));
                    if(primitive.kind==1)p.rect(x,y,w,h,primitive.color);
                    else if(auto sprite=art.get(primitive.bank,primitive.frame))p.sprite(sprite->pixels.data(),sprite->width,sprite->height,x,y,w,h);
                }
            }
        }
    }
    void draw_race(kar_pixels::Surface& p)const {
        const auto projected=view();
        const auto* art=artwork?artwork->get():nullptr;
        p.rect(0,0,240,320,0x50a9f2);p.rect(0,195,240,125,0x9dce83);
        for(int y=80;y<170;++y){int mix=(y-80)/3;p.rect(0,y,240,1,unsigned((80+mix)<<16)|unsigned((169+mix)<<8)|242u);}
        if(!art)p.circle(191,72,15,0xfff2b5);
        for(int i=0;i<20;i++){double x=i*14;double h=5+(i*13)%16;if(!art)p.rect(x,197-h,14,h,0x5a967c);}
        if(art){
            // Background sheets wrap independently of the road; integer offsets
            // preserve their original pixels during camera motion.
            if(auto sky=art->get(2080,0))for(int x=-sky->width;x<240+sky->width;x+=sky->width)sky->draw(p,x-sky->x,80-sky->y);
            if(auto panorama=art->get(2084,0)){
                int scroll=-(road.segment*3+steering.camera/4)%panorama->width;
                for(int x=scroll-panorama->width;x<240+panorama->width;x+=panorama->width)panorama->draw(p,x-panorama->x,202-panorama->height-panorama->y);
            }
        }
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
                p.rect(0,yy,240,1,0x9dce83);
                p.rect(center-half-depth*140,yy,half*2+depth*280,1,0xe5eee1);
                int shade=std::clamp(32+(yy-150)/6,25,65);
                p.rect(center-half,yy,half*2,1,unsigned((95-shade)<<16)|unsigned((161-shade)<<8)|unsigned(181-shade));
                for(int x=std::max(0,int(center-half));x<std::min(240,int(center+half));++x){
                    uint32_t hash=uint32_t(x*374761393u)^uint32_t((int(near_distance)+absolute(road))/32)*668265263u;hash=(hash^(hash>>13))*1274126177u;
                    if((hash&7)==0){int value=int((hash>>8)&7)-3;auto& pixel=p.pixels[yy*240+x];int r=int((pixel>>16)&255)+value,g=int((pixel>>8)&255)+value,b=int(pixel&255)+value;pixel=0xff000000u|unsigned(r<<16)|unsigned(g<<8)|unsigned(b);}
                }
                for(int sign:{-1,1})p.rect(center+sign*depth*22-depth*8,yy,std::max(1.,depth*16),1,0xfff2a1);
                if(stripe)for(int lane:{-1,1})p.rect(center+lane*half*.5-depth*6,yy,std::max(1.,depth*12),1,0xe4f1ed);
            }
        }
        if(art && !art->scene.empty())draw_scene(p,*art,projected);
        for(int i=28;i>=1 && (!art || art->scene.empty());--i){
            double distance=i*1400-std::fmod(double(absolute(road)),1400.);auto q=project(projected,distance);
            if(art){
                double scale=q.depth*8;
                unsigned bank=i%4==0?3042:3007;
                int frame=bank==3007?0:(absolute(road)/1400+i)%3;
                if(auto sprite=art->get(bank,frame)){
                    int center=int((sprite->x+sprite->width*.5)*scale),bottom=int((sprite->y+sprite->height)*scale);
                    sprite->draw(p,int(q.x-q.half-q.depth*550)-center,int(q.y)-bottom,scale);
                    sprite->draw(p,int(q.x+q.half+q.depth*650)-center,int(q.y)-bottom,scale);
                }
                continue;
            }
            if(i%3==0){palm(p,q.x-q.half-q.depth*550,q.y,q.depth);palm(p,q.x+q.half+q.depth*650,q.y,q.depth);}
            if(i%4==0){double x=q.x+q.half+q.depth*500,w=q.depth*1000,h=q.depth*1800;p.rect(x,q.y-h,w,h,0xeee1bf);p.rect(x,q.y-h,w,3*q.depth*30,0xbb7658);for(int j=0;j<3;j++)p.rect(x+w*(j+.2)/3,q.y-h*.7,w*.12,h*.25,0x68909b);}
        }
        for(size_t i=0;i<kar_course::pickups.size();++i){const auto& pick=kar_course::pickups[i];if(picked[i]==road.lap)continue;Road at{pick.segment,0,road.lap,pick.lateral};int distance=at.gap_to(road,segments);if(distance<100 || distance>16000)continue;auto q=project(projected,distance,pick.lateral);if(q.x<8 || q.x>225)continue;double size=std::clamp(q.depth*110,4.,16.);if(art){art->draw(p,pick.kind==0?3010:3009,pick.kind==0?int(elapsed*8)%2:0,int(q.x),int(q.y),std::clamp(q.depth*6,.08,1.));continue;}p.text(q.x-size*.4,q.y,size,pick.kind==0?"N":"$",pick.kind==0?0x55eeff:0xffeb61);}
        for(const auto& block:roadblocks){if(!block.active)continue;int gap=absolute(block.road)-absolute(road);if(gap<0 || gap>20000)continue;auto q=project(projected,gap,block.road.lateral);double w=q.depth*720,h=q.depth*240;p.rect(q.x-w/2,q.y-h,w,h,0xe0e4df);for(int i=0;i<5;i++)p.rect(q.x-w/2+w*i/5,q.y-h,w/10,h,0xe76e3f);p.circle(q.x-w*.3,q.y-h*1.25,std::max(1.,h*.12),0xffae34);p.circle(q.x+w*.3,q.y-h*1.25,std::max(1.,h*.12),0xffae34);}
        std::array<int,12> order{};for(int i=0;i<12;i++)order[i]=i;
        std::sort(order.begin(),order.end(),[&](int a,int b){return racers[a].road.gap_to(road,segments)>racers[b].road.gap_to(road,segments);});
        constexpr unsigned colors[]={0x5795bd,0xd75543,0xd2b45a,0x778994,0x8b6faf,0xe8e9df,0x628966};
        constexpr unsigned rival_banks[]={2022,2011,2017,2019,2028,2022,2011};
        for(int i:order){const auto& r=racers[i];if(r.police && !police_active)continue;int gap=r.road.gap_to(road,segments);if(gap<0 || gap>16000 || r.impact.wrecked)continue;auto q=project(projected,gap,r.road.lateral);if(q.x< -80 || q.x>320)continue;if(art){unsigned bank=r.police?2015:r.traffic?2011:rival_banks[i%7];art->draw(p,bank,r.oncoming?11:4,int(q.x),int(q.y),vehicle_scale(q));}
            else vehicle_art(p,q.x,q.y,vehicle_scale(q),colors[i%7],0,r.police,r.oncoming);}
        if(motion.stage>0){p.line(101,286,97,308,0x91ecff,5);p.line(139,286,143,308,0x91ecff,5);}
        if(steering.drift){p.line(91,283,80-steering.drift*8,312,0x596264,2);p.line(145,283,146-steering.drift*8,312,0x596264,2);}
        if(invulnerable<=0 || int(elapsed*12)%2==0){
            int yaw=std::clamp((steering.body-steering.camera)/28,-4,4),y=282-std::min(70,jump_height);
            if(art){art->draw(p,2005,yaw+78,120,y);if(phase==Phase::busted)art->draw(p,2095,0,120,y);art->draw(p,1,yaw+4,120,y);art->draw(p,2,(4-std::abs(yaw))*2+(int(elapsed*20)&1),120,y,1,yaw>0);}
            else vehicle_art(p,120,y,1,0x689fbd,yaw);
        }
        if(art){
            art->text(p,2102,std::to_string(position),3,3);art->text(p,2100,"/8",31,6);
            art->draw(p,1056,1,88,35);art->text(p,2101,std::to_string(std::clamp(road.lap,1,3))+"/3",104,5);
            art->text(p,2100,std::to_string(score)+" $",199,7);
            art->draw(p,1055,6,0,319);art->draw(p,1055,7,240,319);
            p.rect(23,311,68.*motion.nitro/(150*unit),3,motion.stage==4?0xffa4f1:0x5bdefb);
            art->text(p,2101,std::to_string(motion.kph()),143,302);art->text(p,2100,"KM/H",207,312);
            art->text(p,2100,std::to_string(std::clamp(motion.kph()/50+1,1,6)),210,299);
            double angle=-2.5+std::clamp(motion.kph()/320.,0.,1.)*2.5;
            p.line(211,320,211+std::cos(angle)*23,320+std::sin(angle)*23,0xfff4fc,1);
            if(heat>0){for(int i=0;i<5;i++)p.rect(188+i*9,25,6,3,heat>i*20?0xff6784:0x254e83);}
        }else{
        p.text(5,19,21,std::to_string(position),0xffe560);p.text(24,16,11,"/8",0x233e49);
        p.rect(96,2,48,19,0x256477);p.text(104,16,12,std::to_string(std::clamp(road.lap,1,3))+" / 3",0xe9f4d9);
        p.text(166,14,10,std::to_string(score)+" $",0xffe560);p.text(181,34,9,"WANTED",heat>=70?0xffd34c:0x213d47);
        for(int i=0;i<5;i++)p.rect(168+i*2,26,1.5,8,heat>=20*(i+1)?0xec623c:0x456473);
        p.rect(48,305,144,12,0x1c3c4a);p.rect(50,307,140.*motion.nitro/(150*unit),8,motion.stage==4?0xffd85e:0x4dbbea);
        p.text(183,304,20,std::to_string(motion.kph()),0xffe363);p.text(188,318,8,"KM/H",0xe4f4e9);
        }
        if(notice_time>0 && phase!=Phase::busted){
            if(art)art->text(p,2101,notice,std::max(3,120-int(notice.size())*6),78);
            else p.text(36,87,14,notice,0xffec8c);
        }
        if(phase==Phase::ready){p.rect(16,83,208,78,0x174757);p.text(78,104,22,"KAR",0xe7f6e8);p.text(30,124,11,"BAHAMAS  /  THREE LAPS",0x9ddcf1);p.text(29,141,10,"C: vehicle  "+std::to_string(kar_physics::vehicles[vehicle].maximum)+" km/h",0xe7f6e8);p.text(49,154,10,"ENTER / SPACE TO RACE",0xffdc71);}
        if(phase==Phase::countdown)p.text(105,118,42,std::to_string(std::max(1,int(std::ceil(phase_time)))),0xffe166);
        if(phase==Phase::busted){
            p.rect(26,66,188,2,0x54dfff);p.rect(26,99,188,2,0xf285d9);
            if(art){art->text(p,2101,"BUSTED",83,73);art->text(p,2100,"FINE $"+std::to_string(score/4),77,94);}
            else{p.text(83,88,9,"BUSTED",0xffcf70);p.text(77,109,9,"FINE $"+std::to_string(score/4),0xffffff);}
        }
        if(over){p.rect(19,94,202,78,0x164655);p.text(39,116,19,won?"PODIUM FINISH!":"RACE COMPLETE",0xffdf77);p.text(47,140,13,"POSITION "+std::to_string(position)+" / 8",0xe8f3df);p.text(45,159,11,"R TO RACE AGAIN",0x92d7ef);}
    }
    mutable kar_pixels::Surface framebuffer;
    template<class P>void draw(P& output)const {
        if(artwork && !artwork->get()){
            bool ready=artwork->ready.load(std::memory_order_acquire);
            output.text(28,100,23,"KAR",0x8ce9b3);
            output.text(28,155,14,ready?"Artwork download failed":"Downloading Kar artwork…",0xe0f5e8);
            output.text(28,190,12,ready?"Check your connection; press R to retry.":"First launch only. Your trial is paused.",0x8dada1);
            return;
        }
        draw_race(framebuffer);
        if constexpr(requires{output.image(57,38,306,408,framebuffer.pixels.data(),240,320);}){
            output.image(57,38,306,408,framebuffer.pixels.data(),240,320);
        }else{
            // Diagnostic painters still see the same raster, never a different
            // vector implementation. Merge same-colored runs for compact output.
            Canvas<P> canvas{output};
            for(int y=0;y<320;++y)for(int x=0;x<240;){int end=x+1;auto color=framebuffer.pixels[y*240+x];while(end<240 && framebuffer.pixels[y*240+end]==color)++end;canvas.rect(x,y,end-x,1,color&0xffffff);x=end;}
        }
        output.text(15,467,10,"Arrows/WASD: drive  Space/Up: nitro  Down+steer: drift",0x8dada1);
    }
};
}
