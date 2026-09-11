#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

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
inline constexpr std::array<Vehicle,3> vehicles{{
    {313,65,50,34,{0,70,100,150,190,230,313},{0,1000,2000,2100,2400,2700,3000}},
    {322,75,45,27,{0,70,100,150,190,250,322},{0,1200,2300,3100,3300,3500,3600}},
    {326,70,35,30,{0,70,100,140,180,245,326},{0,2000,2300,3400,3700,4000,4300}}
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
        int target=braking?0:from_kph(car.maximum)*(100+extra[stage])/100;
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
            if(drift_entering){body+=sign*degrees(3);if(sign*body>=bend){body=sign*bend;drift_entering=false;}}
            else if(std::abs(remaining_curve)<std::abs(body)){drift_exiting=true;body-=sign*(degrees(3)>>1);}
            else if(!drift_exiting){body+=sign*(degrees(3)>>2);body=std::clamp(body,-degrees(40),degrees(40));}
            // While sliding, steering changes the sideways motion independently of body yaw.
            heading=direction?direction:sign*2;
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

}
