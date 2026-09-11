#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <span>

namespace kalwer::games::kar_physics {
// Asphalt 6 Java's longitudinal model uses Q12 metres/second and Q12 seconds.
// Keep integer truncation: converting the equations to floats changes gear timing.
constexpr int unit=4096;
constexpr int from_kph(int speed){return speed*unit*10/36;}
constexpr int from_ms(int ms){return ms*unit/1000;}
struct Vehicle {
    int maximum,nitro_duration_percent,braking,steering_degrees;
    std::array<int,7> gears;
    std::array<int,7> gear_ms;
};
inline constexpr std::array<Vehicle,4> vehicles{{
    {313,65,50,34,{0,70,100,150,190,230,313},{0,1000,2000,2100,2400,2700,3000}},
    {322,75,45,27,{0,70,100,150,190,250,322},{0,1200,2300,3100,3300,3500,3600}},
    {326,70,35,30,{0,70,100,140,180,245,326},{0,2000,2300,3400,3700,4000,4300}},
    {260,70,50,26,{0,70,100,140,180,220,260},{0,1300,2500,2800,3100,3400,3700}}
}};
struct Motion {
    int vehicle=0,speed=0,gear=1,nitro=25*unit,stage=0,boost_left=0,drain_per_ms=0;
    std::int64_t travel=0;
    void reset(int selected){*this=Motion{};vehicle=std::clamp(selected,0,int(vehicles.size())-1);}
    int kph()const{return (speed*36/10+unit/2)/unit;}
    int road_speed()const {
        constexpr int threshold=from_kph(250);
        return speed<=threshold?speed:threshold+(speed-threshold)/3;
    }
    void refill(int amount){nitro=std::clamp(nitro+amount*unit,0,150*unit);}
    bool boost(){
        // A second/third tap commits another stage from the uncommitted reserve.
        int available=nitro-(boost_left*1000/unit)*drain_per_ms;
        int next=available==150*unit?4:stage==0?1:stage<3?stage+1:0;
        constexpr int costs[]={0,25,40,60,150};
        constexpr int durations[]={0,4000,5000,6000,7000};
        if(!next || available<costs[next]*unit)return false;
        nitro=available;stage=next;
        int duration=durations[stage]*vehicles[vehicle].nitro_duration_percent/100;
        boost_left=from_ms(duration);drain_per_ms=costs[stage]*unit/duration;
        return true;
    }
    void step(int milliseconds,bool braking=false,bool drifting=false,int surface_loss=0) {
        milliseconds=std::clamp(milliseconds,0,100);if(!milliseconds)return;
        int dt=from_ms(milliseconds);const auto& car=vehicles[vehicle];
        while(gear<6 && speed>=from_kph(car.gears[gear]))++gear;
        while(gear>1 && speed<from_kph(car.gears[gear-1]))--gear;
        constexpr int extra[]={0,10,20,30,50};
        int target=braking?0:from_kph(car.gears[gear])+from_kph(car.maximum)*extra[stage]/100;
        target=target*(100-std::clamp(surface_loss,0,100))/100;
        int rise=from_kph(car.gears[gear])-from_kph(car.gears[gear-1]);
        constexpr int acceleration_bonus[]={0,30,50,80,250};
        int acceleration=rise*(100+acceleration_bonus[stage])/(from_ms(car.gear_ms[gear])*100);
        if(braking) speed=std::max(0,speed-((car.braking*dt)>>(drifting?0:1)));
        else if(speed>target) {int drop=dt*acceleration;speed=std::max(target,speed-(drop>>1)-(drop>>2));}
        else speed=std::min(target,speed+dt*acceleration);
        // Longitudinal travel before track curvature and heading projection.
        travel+=(std::int64_t(road_speed())*dt+unit/2)/unit;
        if(boost_left>0){boost_left-=dt;nitro=std::max(0,nitro-milliseconds*drain_per_ms);if(boost_left<=0){boost_left=0;stage=0;}}
    }
};
// Heading and camera angles use 2048 units per turn, as in the phone renderer.
struct Steering {
    static constexpr int turn=2048;
    static constexpr int degrees(int value){return value*turn/360;}
    int heading=0,body=0,camera=0,direction=0,drift=0,held_time=0;
    bool drift_entering=false,drift_exiting=false;
    int limit(int speed)const {
        int low=from_kph(120),high=from_kph(300);
        return speed>high?degrees(20):degrees(25)-(degrees(25)-degrees(20))*(speed-low)/(high-low);
    }
    void reset(){*this=Steering{};}
    void controls(int dt,int speed,int handling,int input,int curve,int remaining_curve,
                  bool manual_drift=false,bool airborne=false,bool crashed=false) {
        input=std::clamp(input,-1,1);
        if(input!=direction)held_time=0;
        direction=input;
        if(direction)held_time+=dt;else held_time=0;
        if(drift && (speed<from_kph(100) || crashed || !curve || drift*curve>0)){
            drift=0;drift_entering=drift_exiting=false;heading=body=held_time=0;
        }
        if(!drift && !airborne && !crashed && speed>=from_kph(100) &&
           (manual_drift || held_time>4000) && direction*curve<0){
            drift=direction;drift_entering=true;drift_exiting=false;heading=0;
        }
        int delta=((degrees(handling)*2*dt+unit/2)/unit)/2;
        if(drift)delta*=2;
        if(!drift){
            if(!direction || !speed){int ease=(heading-body)>>1;body+=ease;camera+=ease;}
            else{heading+=direction*delta;body+=direction*delta;}
            int cap=limit(speed);heading=std::clamp(heading,-cap,cap);body=std::clamp(body,-cap,cap);
        }else{
            int sign=drift;
            int bend=std::min(std::abs(remaining_curve),degrees(40));
            if(drift_entering){
                if(sign*body>bend){body=sign*bend;drift_entering=false;}
                else body+=sign*degrees(3);
            }else if(sign*body>std::abs(remaining_curve-curve)){
                drift_exiting=true;
                body-=sign*(degrees(3)>>1);
                if(sign*body<std::abs(remaining_curve-curve))body=sign*std::abs(remaining_curve-curve);
            }else if(!drift_exiting){
                // Countersteering narrows the slide; holding into it widens it.
                // Body yaw and sideways velocity are independent during a drift.
                heading=direction?direction:sign*2;
                if(direction==-sign){
                    body-=sign*(degrees(3)>>2);
                    if(sign*body<degrees(25))body=sign*degrees(25);
                }else{
                    body+=sign*(degrees(3)>>2)*(direction==sign?2:1);
                    if(sign*body>degrees(40))body=sign*degrees(40);
                }
            }
        }
    }
    void track_rotation(int rotation,int speed){
        if(!drift){heading+=rotation;body+=rotation;camera+=rotation;int cap=limit(speed);heading=std::clamp(heading,-cap,cap);body=std::clamp(body,-cap,cap);}
        int offset=body-camera;
        if(drift){
            if(drift_entering){if(offset>degrees(25))camera=body-degrees(25);else if(offset< -degrees(25))camera=body+degrees(25);}
            else camera=drift<0?std::min(0,body+degrees(25)):std::max(0,body-degrees(25));
        }else if(!direction){body+=offset>0?-std::min(5,offset):std::min(5,-offset);}
        else if(offset>degrees(15))camera=body-degrees(15);
        else if(offset< -degrees(15))camera=body+degrees(15);
        else camera+=offset/8;
        if(!drift)camera=std::clamp(camera,-degrees(25),degrees(25));
    }
};

// The road uses 1024-unit segments. A lateral position changes the distance
// around a bend: the inner lane travels farther along the centreline per tick.
struct RoadPosition {
    int segment=0,offset=0,lap=0,lateral=0;
    static constexpr int segment_length=1024;
    static int cosine(int angle) {
        angle=((angle%2048)+2048)%2048;
        int quadrant=angle/512,within=angle%512;
        if(quadrant&1)within=512-within;
        int magnitude=int(std::cos(within*3.14159265358979323846/1024.)*unit);
        return quadrant==1 || quadrant==2?-magnitude:magnitude;
    }
    static int rounded_q12(std::int64_t value){return int((value+unit/2)>>12);}
    int gap_to(const RoadPosition& other,int count)const {
        if(count<=0)return 0;
        int segments=segment-other.segment;
        if(std::abs(segments)>count/2)segments+=segments>0?-count:count;
        return segments*segment_length+offset-other.offset;
    }
    int project(int speed,int dt,int heading,bool drifting) {
        constexpr int threshold=from_kph(250);
        int road_speed=speed<=threshold?speed:threshold+(speed-threshold)/3;
        int distance=rounded_q12(std::int64_t(road_speed)*dt)/40;
        int sideways=rounded_q12(std::int64_t(cosine(512-heading))*distance);
        if(drifting && heading) {
            int slide_speed=std::abs(heading)==2?5688:17066;
            sideways=rounded_q12(std::int64_t(slide_speed)*dt)/40;
            if(heading<0)sideways=-sideways;
        }
        lateral+=sideways;
        return drifting?distance:rounded_q12(std::int64_t(cosine(heading))*distance);
    }
    int advance(int distance,std::span<const int> curves,bool rotate=true) {
        if(curves.empty())return 0;
        int length=int(curves.size());
        segment=(segment%length+length)%length;
        int curve=curves[segment];
        if(curve) {
            constexpr int radius_scale=(2048*unit)/(12867*2);
            int radius=rounded_q12((std::int64_t(radius_scale)*segment_length*unit)/std::abs(curve));
            int lane_radius=radius+(curve>0?lateral:-lateral);
            if(lane_radius) {
                auto angular=(std::int64_t(radius_scale)*distance*unit)/lane_radius;
                distance=rounded_q12(radius*angular/radius_scale);
            }
        }
        int rotation=0;
        if(rotate && distance>0) {
            int remaining=distance,part_segment=segment,part_offset=offset;
            while(remaining>0) {
                int part=std::min(remaining,segment_length-part_offset);
                rotation+=rounded_q12(std::int64_t(curves[part_segment])*part/segment_length*2252);
                remaining-=part;part_offset=0;part_segment=(part_segment+1)%length;
            }
        }
        std::int64_t absolute=std::int64_t(offset)+distance;
        while(absolute>=segment_length){absolute-=segment_length;if(++segment==length){segment=0;++lap;}}
        while(absolute<0){absolute+=segment_length;if(--segment<0){segment=length-1;--lap;}}
        offset=int(absolute);
        return rotation;
    }
};

struct CollisionBody {
    RoadPosition position,previous;
    int left_extent=0,right_extent=0,length=0,height=0;
    bool active=true,wrecked=false;

    bool overlaps_laterally(const CollisionBody& other)const {
        int x=position.lateral,other_x=other.position.lateral;
        if(x==other_x)return true;
        if(x>other_x)return previous.lateral<other.previous.lateral || x-left_extent<other_x+other.right_extent;
        return previous.lateral>other.previous.lateral || x+right_extent>other_x-other.left_extent;
    }
    enum class Contact {none,side,front,rear};
    Contact contact(const CollisionBody& other,int segments)const {
        if(!active || !other.active || wrecked || other.wrecked ||
           std::abs(height-other.height)>100 || !overlaps_laterally(other))return Contact::none;
        int gap=position.gap_to(other.position,segments);
        int prior=previous.gap_to(other.previous,segments);
        if(prior>=0 && prior<other.length && gap<other.length)return Contact::side;
        if(prior>=0 && gap-other.length>-3072 && gap<other.length)return Contact::front;
        if(prior<=0 && prior>-length && gap>-length)return Contact::side;
        if(prior<=0 && gap<3072 && gap>-length)return Contact::rear;
        return Contact::none;
    }
};

// Impact response is separate from collision detection so rendering can consume
// a hit once without applying another speed penalty on every rendered frame.
struct ImpactResponse {
    enum class Hit {front,rear,head_on,from_left,from_right,knockdown};
    int cooldown=0,wreck_time=0,vertical_speed=0,height=0;
    bool wrecked=false;
    bool apply(Hit hit,int relative_speed,Motion& motion,Steering& steering) {
        if(wrecked)return false;
        bool protected_boost=motion.stage>=3;
        if(hit==Hit::head_on && !protected_boost) {
            if(motion.speed>11377)motion.speed>>=2;
            if(relative_speed>=from_kph(80))hit=Hit::knockdown;
        }
        if(hit==Hit::knockdown){
            wrecked=true;wreck_time=2*unit;cooldown=wreck_time;
            vertical_speed=200*unit;height=1;
            motion.stage=motion.boost_left=0;
            return true;
        }
        if(!protected_boost && motion.speed>11377) {
            if(hit==Hit::front)motion.speed-=motion.speed>>2;
        }
        if(hit==Hit::from_left || hit==Hit::from_right) {
            int sign=hit==Hit::from_left?1:-1;
            steering.heading=steering.body=sign*Steering::degrees(2);
            steering.camera=steering.body-sign*Steering::degrees(5);
        }
        if(hit!=Hit::rear)cooldown=1638;
        return true;
    }
};

}
