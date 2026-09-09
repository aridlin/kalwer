#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <random>
#include <string>
#include <vector>

namespace kalwer::games {
struct Peggle {
    static constexpr double pi = 3.141592653589793;
    static constexpr double aim_limit = 85 * pi / 180;
    static constexpr double gravity = 225, launch_speed = 315, step = 1.0 / 480;
    enum class Power { none, blast, guide, catch_ball, bonus };
    struct Peg {
        double x, y;
        bool orange = false, hit = false, removed = false;
        double angle = 0, half = 0, radius = 8, age = 0;
        Power power = Power::none;
    };
    struct Particle { double x,y,vx,vy,life,total; unsigned color; };
    struct Label { double x,y,life; std::string text; unsigned color; };
    struct Point { double x,y; };
    struct Contact { double nx=0,ny=0,depth=0; };
    std::vector<Peg> pegs;
    std::vector<Particle> particles;
    std::vector<Label> labels;
    std::deque<Point> trail;
    struct GuidePoint {double x,y,time;};
    mutable std::vector<GuidePoint> preview;
    mutable double preview_aim=999;
    mutable int preview_target=-1;
    std::mt19937 effects{731};
    double ballX=210,ballY=105,vx=0,vy=0,aim=0,cannon=0,bucket=210;
    double clock=0,accumulator=0,flight=0,stuck=0,trail_clock=0,kick=0,bucket_flash=0;
    int balls=10,score=0,shot_score=0,shot_hits=0,free_tier=0,orange_total=0,orange_hit=0;
    int guide_shots=0,catch_shots=0,layout=-1;
    bool flying=false,started=false,over=false,won=false,shot_guide=false,shot_catch=false;
    inline static constexpr std::array<int,3> free_scores{2500,7500,15000};
    inline static constexpr std::array<const char*,5> names{"ORBIT","CASCADE","BLOOM","FORTRESS","PRISM"};

    void reset(std::mt19937& random,bool prism=false) {
        preview_aim=999;
        layout=prism?4:layout<0 ? int(random()%4) : (layout+1)%4;
        balls=10; score=shot_score=shot_hits=free_tier=orange_hit=0;
        clock=accumulator=flight=stuck=trail_clock=kick=bucket_flash=0;
        ballX=bucket=210; ballY=105; vx=vy=aim=cannon=0;
        flying=started=over=won=shot_guide=shot_catch=false; guide_shots=catch_shots=0;
        pegs.clear(); particles.clear(); labels.clear(); trail.clear();
        auto add=[&](double x,double y,bool brick=false,double angle=0) {
            double bound=brick?16:8;
            if(x-bound<35 || x+bound>385 || y-bound<143 || y+bound>406) return;
            for(const auto& p:pegs) if(std::hypot(p.x-x,p.y-y)<p.half+p.radius+bound+3) return;
            pegs.push_back({x,y,false,false,false,angle,brick?11.:0.,brick?5.:8.});
        };
        if(layout==0) {
            for(int ring=0;ring<3;++ring) {
                double radius=48+ring*47;
                for(int i=0;i<12+ring*8;++i) {
                    double a=2*pi*i/(12+ring*8)+ring*.12;
                    add(210+radius*std::cos(a),272+radius*.85*std::sin(a),ring==1 && i%3==0,a+pi/2);
                }
            }
        } else if(layout==1) {
            for(int row=0;row<6;++row) for(int col=0;col<9;++col) {
                double x=51+col*39+(row%2)*7;
                add(x,157+row*42+std::sin(col*.8+row)*10,(col+row)%3==0,(row%2?-.22:.22));
            }
        } else if(layout==2) {
            for(int petal=0;petal<6;++petal) {
                double a=petal*pi/3+.085, r=108;
                add(210+std::cos(a)*r,275+std::sin(a)*r*.8,true,a+pi/2);
            }
            for(int petal=0;petal<6;++petal) for(int i=0;i<9;++i) {
                double a=petal*pi/3+(i-4)*.085, r=43+i*13;
                add(210+std::cos(a)*r,275+std::sin(a)*r*.8,i==5,a+pi/2);
            }
            for(int i=0;i<8;++i) add(210+std::cos(i*pi/4)*24,275+std::sin(i*pi/4)*24);
            for(int i=0;i<24;++i) add(210+std::cos(i*pi/12)*160,275+std::sin(i*pi/12)*136);
        } else if(layout==3) {
            for(int row=0;row<5;++row) for(int col=0;col<9;++col) {
                double x=53+col*39+(row%2)*8;
                double y=158+row*46+std::abs(col-4)*5;
                add(x,y,(row+col)%2==0,(col-4)*.055);
            }
            for(int i=0;i<7;++i) add(100+i*36,396,false);
        } else {
            for(int ring=0;ring<3;ring++)for(int i=0;i<16;i++){
                double a=i*pi/8+pi/8, radius=50+ring*47;
                add(210+std::cos(a)*radius,275+std::sin(a)*radius*.82,ring==1 && i%2==0,a+pi/2);
            }
            for(int i=0;i<9;i++)add(54+i*39,154+std::abs(i-4)*9,i%2==0,(i<4?-.3:.3));
            for(int i=0;i<8;i++)add(70+i*40,395,false);
        }
        std::vector<int> order;
        for(int i=0;i<int(pegs.size());++i) order.push_back(i);
        std::shuffle(order.begin(),order.end(),random);
        orange_total=std::min(18,int(pegs.size())/3);
        for(int i=0;i<orange_total;++i) pegs[order[i]].orange=true;
        for(int i=0;i<4 && orange_total+i<int(order.size());++i)
            pegs[order[orange_total+i]].power=static_cast<Power>(i+1);
    }
    int multiplier() const {
        double cleared=orange_total?double(orange_hit)/orange_total:0;
        return cleared>=.8?10:cleared>=.6?5:cleared>=.4?3:cleared>=.2?2:1;
    }
    int combo() const { return std::min(5,1+shot_hits/5); }
    double bucket_half() const { return (flying?shot_catch:catch_shots>0)?61:34; }
    void label(double x,double y,std::string text,unsigned color=0x8ce9b3) {
        if(labels.size()>=16) labels.erase(labels.begin());
        labels.push_back({x,y,1.15,std::move(text),color});
    }
    void burst(double x,double y,unsigned color,int count=7) {
        for(int i=0;i<count && particles.size()<180;++i) {
            double a=(effects()%6284)/1000., speed=20+effects()%55, life=.25+(effects()%250)/1000.;
            particles.push_back({x,y,std::cos(a)*speed,std::sin(a)*speed,life,life,color});
        }
    }
    void award(int points) {
        score+=points; shot_score+=points;
        while(free_tier<int(free_scores.size()) && shot_score>=free_scores[free_tier]) {
            ++balls; ++free_tier; bucket_flash=.65; label(154,129,"SCORE +1 BALL");
        }
    }
    void hit(size_t index) {
        auto& p=pegs[index];
        if(p.hit || p.removed) return;
        p.hit=true; p.age=0; ++shot_hits;
        if(p.orange) ++orange_hit;
        const int points=(p.power==Power::bonus?500:p.orange?150:50)*multiplier()*combo();
        award(points);
        unsigned color=p.orange?0xffa657:p.power==Power::none?0x67bce0:0xb8f5ac;
        burst(p.x,p.y,color);
        if(p.orange || p.power!=Power::none) label(p.x-10,p.y-11,"+"+std::to_string(points),color);
        if(p.power==Power::guide) { guide_shots=3; shot_guide=true; label(152,148,"GUIDE: 3 SHOTS",0x67e5de); }
        if(p.power==Power::catch_ball) { catch_shots=3; shot_catch=true; label(149,148,"WIDE CATCH: 3 SHOTS"); }
        if(p.power==Power::blast) {
            label(p.x-26,p.y-25,"BLAST",0xa8ed75); burst(p.x,p.y,0xa8ed75,18);
            for(size_t i=0;i<pegs.size();++i)
                if(std::hypot(pegs[i].x-p.x,pegs[i].y-p.y)<=64) hit(i);
        }
    }
    static Contact contact(const Peg& p,double x,double y) {
        const double reach=p.half+p.radius+6;
        if(std::abs(x-p.x)>reach || std::abs(y-p.y)>reach)return {};
        double ax=std::cos(p.angle), ay=std::sin(p.angle);
        double along=std::clamp((x-p.x)*ax+(y-p.y)*ay,-p.half,p.half);
        double dx=x-(p.x+ax*along),dy=y-(p.y+ay*along),distance=std::hypot(dx,dy);
        double depth=p.radius+6-distance;
        if(depth<=0) return {};
        if(distance<1e-8) return {-ay,ax,depth};
        return {dx/distance,dy/distance,depth};
    }
    // The preview and live ball use identical gravity, collision normals and restitution.
    static bool integrate(double& x,double& y,double& dx,double& dy,double dt,const std::vector<Peg>& board,int* first=nullptr) {
        dy+=gravity*dt; x+=dx*dt; y+=dy*dt;
        if(x<36) { x=36; dx=std::abs(dx)*.91; }
        if(x>384) { x=384; dx=-std::abs(dx)*.91; }
        if(y<90) { y=90; dy=std::abs(dy)*.88; }
        bool touched=false;
        // Two solver passes separate the ball cleanly at brick/peg junctions.
        for(int pass=0;pass<2;++pass) for(size_t i=0;i<board.size();++i) if(!board[i].removed) {
            auto c=contact(board[i],x,y);
            if(c.depth<=0) continue;
            x+=c.nx*(c.depth+.015); y+=c.ny*(c.depth+.015);
            double normal=dx*c.nx+dy*c.ny;
            if(normal<0) {
                double tangent=-dx*c.ny+dy*c.nx;
                dx=-.87*normal*c.nx-.985*tangent*c.ny;
                dy=-.87*normal*c.ny+.985*tangent*c.nx;
            }
            if(first && *first<0) *first=int(i);
            touched=true;
        }
        double speed=std::hypot(dx,dy);
        if(speed>650) { dx*=650/speed; dy*=650/speed; }
        return touched;
    }
    void shoot() {
        preview_aim=999;
        if(flying || over) return;
        started=flying=true; --balls; flight=stuck=0; shot_score=shot_hits=free_tier=0;
        shot_guide=guide_shots>0; shot_catch=catch_shots>0;
        if(guide_shots) --guide_shots;
        if(catch_shots) --catch_shots;
        ballX=210; ballY=105; vx=std::sin(aim)*launch_speed; vy=std::cos(aim)*launch_speed;
        trail.clear(); kick=1;
    }
    void finish_shot(bool caught) {
        if(caught) { ++balls; bucket_flash=.7; label(bucket-25,410,"CATCH +1 BALL"); burst(bucket,428,0x8ce9b3,12); }
        flying=false;
        for(auto& p:pegs) if(p.hit) { p.removed=true; burst(p.x,p.y,p.orange?0xffa657:0x67bce0,3); }
        won=std::none_of(pegs.begin(),pegs.end(),[](const Peg& p){return p.orange && !p.removed;});
        over=won || balls<=0;
        if(won) { score+=balls*500; label(147,197,"CLEAR +"+std::to_string(balls*500)); }
    }
    void physics(double dt) {
        clock+=dt; bucket=210+125*std::sin(clock*.95);
        cannon+=(aim-cannon)*(1-std::exp(-18*dt)); kick=std::max(0.,kick-dt*6);
        bucket_flash=std::max(0.,bucket_flash-dt);
        for(auto& p:pegs) if(p.hit && !p.removed) p.age+=dt;
        for(auto& p:particles) { p.life-=dt; p.x+=p.vx*dt; p.y+=p.vy*dt; p.vy+=65*dt; }
        std::erase_if(particles,[](const Particle& p){return p.life<=0;});
        for(auto& l:labels) { l.life-=dt; l.y-=13*dt; }
        std::erase_if(labels,[](const Label& l){return l.life<=0;});
        if(!flying || over) return;
        flight+=dt;
        double oldY=ballY;
        int first=-1;
        bool touching=integrate(ballX,ballY,vx,vy,dt,pegs,&first);
        if(first>=0) hit(size_t(first));
        // Score any simultaneous contact, without counting an already lit peg twice.
        for(size_t i=0;i<pegs.size();++i) if(!pegs[i].removed && contact(pegs[i],ballX,ballY).depth>-.02 &&
            std::hypot(ballX-pegs[i].x,ballY-pegs[i].y)<pegs[i].half+pegs[i].radius+6.03) {
            // Capsule contact returns zero for non-overlap, so use its closest point directly.
            auto& p=pegs[i]; double a=std::clamp((ballX-p.x)*std::cos(p.angle)+(ballY-p.y)*std::sin(p.angle),-p.half,p.half);
            if(std::hypot(ballX-p.x-a*std::cos(p.angle),ballY-p.y-a*std::sin(p.angle))<=p.radius+6.03) hit(i);
        }
        stuck=touching && std::hypot(vx,vy)<38 ? stuck+dt : std::max(0.,stuck-dt*.25);
        if(stuck>.35) {
            for(auto& p:pegs) if(p.hit && !p.removed && std::hypot(p.x-ballX,p.y-ballY)<40) { p.removed=true; burst(p.x,p.y,0xd8f5e6,4); }
            stuck=0;
        }
        trail_clock+=dt;
        if(trail_clock>=.018) { trail_clock=0; trail.push_back({ballX,ballY}); if(trail.size()>11) trail.pop_front(); }
        if(ballY>=427 && oldY<427 && vy>0) finish_shot(std::abs(ballX-bucket)<=bucket_half());
        else if(ballY>=440 || flight>=24) finish_shot(false);
    }
    void advance(double dt) {
        accumulator+=dt;
        while(accumulator+1e-10>=step) { physics(step); accumulator=std::max(0.,accumulator-step); }
    }
    void key(int key) {
        if(over) return;
        if(!flying) {
            double delta=(key==1 || key=='a')?-.025:(key==2 || key=='d')?.025:key=='q'?-.006:key=='e'?.006:0;
            aim=std::clamp(aim+delta,-aim_limit,aim_limit);
        }
        if(key==' ' || key==13) shoot();
    }
    void pointer(double x,double y,int button) {
        if(over || flying || x<30 || x>390 || y<80 || y>440) return;
        aim=std::clamp(std::atan2(x-210,std::max(.1,y-105)),-aim_limit,aim_limit);
        if(button==1) shoot();
    }
    static unsigned blend(unsigned a,unsigned b,double t) {
        t=std::clamp(t,0.,1.); unsigned out=0;
        for(int shift:{0,8,16}) out|=unsigned(((a>>shift)&255)*(1-t)+((b>>shift)&255)*t)<<shift;
        return out;
    }
    template<class P> void ring(P& p,double x,double y,double r,unsigned color,double width=1) const {
        for(int i=0;i<20;++i) p.line(x+r*std::cos(i*pi/10),y+r*std::sin(i*pi/10),x+r*std::cos((i+1)*pi/10),y+r*std::sin((i+1)*pi/10),color,width);
    }
    template<class P> void draw(P& p) const {
        constexpr unsigned panel=0x112e29,white=0xe0f5e8,muted=0x8dada1;
        p.text(24,52,13,"Score "+std::to_string(score)+"   Balls "+std::to_string(balls)+"   x"+std::to_string(multiplier()),white);
        p.text(24,67,10,std::string(names[layout])+"   Orange "+std::to_string(orange_total-orange_hit)+"   Shot "+std::to_string(shot_score)+"  x"+std::to_string(combo()),muted);
        int threshold=free_tier<int(free_scores.size())?free_scores[free_tier]:free_scores.back();
        p.rect(30,74,360,3,0x244c40); p.rect(30,74,360*std::min(1.,double(shot_score)/threshold),3,0x8ce9b3);
        p.rect(30,80,360,360,panel);
        if(!flying && !over) {
            if(preview_aim!=aim){
                preview_aim=aim;preview.clear();preview_target=-1;
                double x=210,y=105,dx=std::sin(aim)*launch_speed,dy=std::cos(aim)*launch_speed;
                int bounces=0;double time=0,after=0;
                for(int i=0;i<(guide_shots?1100:530);++i){
                    int peg=-1;integrate(x,y,dx,dy,step,pegs,&peg);time+=step;
                    if(peg>=0 && peg!=preview_target){++bounces;preview_target=peg;}
                    if(bounces)after+=step;
                    if(y>=425 || (bounces && after>(guide_shots?.8:.23)))break;
                    if(i%23==0)preview.push_back({x,y,time});
                }
            }
            for(const auto& point:preview)p.circle(point.x,point.y,1.4,blend(0x6a9f86,panel,point.time/(guide_shots?2.5:1.35)));
            if(preview_target>=0)ring(p,pegs[preview_target].x,pegs[preview_target].y,pegs[preview_target].half+pegs[preview_target].radius+4,0x83b9a0);
        }
        p.circle(210,99,10,0x3a6e56);
        p.line(210-std::sin(cannon)*kick*3,99-std::cos(cannon)*kick*3,210+std::sin(cannon)*19,99+std::cos(cannon)*19,0x8ce9b3,7);
        for(const auto& peg:pegs) if(!peg.removed) {
            unsigned c=peg.orange?0xffa657:peg.power==Power::blast?0xa8ed75:peg.power==Power::guide?0x67e5de:peg.power==Power::catch_ball?0x8ce9b3:peg.power==Power::bonus?0xd99df5:0x67bce0;
            double pulse=peg.hit?std::exp(-peg.age*9)*1.6:0;
            if(peg.hit) c=blend(c,white,.65+.35*std::exp(-peg.age*7));
            double ax=std::cos(peg.angle)*peg.half,ay=std::sin(peg.angle)*peg.half,r=peg.radius+pulse;
            if(peg.half) p.line(peg.x-ax,peg.y-ay,peg.x+ax,peg.y+ay,c,r*2);
            p.circle(peg.x-ax,peg.y-ay,r,c); if(peg.half) p.circle(peg.x+ax,peg.y+ay,r,c);
            p.circle(peg.x-2,peg.y-2,2,blend(c,white,.5));
            if(peg.power!=Power::none && !peg.hit) {
                if(peg.power==Power::blast) { p.line(peg.x-3,peg.y,peg.x+3,peg.y,panel,1.5); p.line(peg.x,peg.y-3,peg.x,peg.y+3,panel,1.5); }
                else if(peg.power==Power::catch_ball) { p.line(peg.x-3,peg.y-2,peg.x-3,peg.y+3,panel,1.5); p.line(peg.x-3,peg.y+3,peg.x+3,peg.y+3,panel,1.5); p.line(peg.x+3,peg.y+3,peg.x+3,peg.y-2,panel,1.5); }
                else ring(p,peg.x,peg.y,3,panel,1.5);
            }
            if(peg.hit && peg.age<.3) ring(p,peg.x,peg.y,peg.radius+3+peg.age*27,blend(c,panel,peg.age/.3));
        }
        double half=bucket_half(); unsigned bc=bucket_flash>0?white:0x8ce9b3;
        p.line(bucket-half,428,bucket-half,435,bc,3); p.line(bucket-half,435,bucket+half,435,bc,3); p.line(bucket+half,435,bucket+half,428,bc,3);
        if(flying) {
            for(size_t i=0;i<trail.size();++i) p.circle(trail[i].x,trail[i].y,1+2.*i/trail.size(),blend(panel,0x9cbfab,.45*i/trail.size()));
            p.circle(ballX,ballY,6,white); p.circle(ballX-1.5,ballY-2,1.8,0xffffff);
        }
        for(const auto& e:particles) p.circle(e.x,e.y,1.1,blend(panel,e.color,e.life/e.total));
        for(const auto& l:labels) p.text(std::clamp(l.x,33.,295.),l.y,10,l.text,blend(panel,l.color,std::min(1.,l.life*3)));
        if(guide_shots || catch_shots) p.text(38,421,9,"Guide "+std::to_string(guide_shots)+"  Wide "+std::to_string(catch_shots),muted);
        p.text(24,467,10,"Aim: mouse/arrows  Q/E: fine  Space: fire",muted);
        p.text(24,483,9,"Free balls: shot 2.5k / 7.5k / 15k or catch",muted);
    }
};
}
