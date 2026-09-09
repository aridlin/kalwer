#include "../games.hpp"
#include <cassert>
#include <iostream>
using namespace kalwer::games;
int main() {
    std::mt19937 random(14); Peggle p;
    for(int layout=0;layout<4;++layout) {
        p.reset(random); assert(p.pegs.size()>=30 && p.orange_total>=10);
        int bricks=0,powers=0;
        for(auto& peg:p.pegs) { bricks+=peg.half>0; powers+=peg.power!=Peggle::Power::none; assert(peg.x>30 && peg.x<390 && peg.y>140 && peg.y<410); }
        assert(bricks && powers==4);
    }
    p.reset(random); p.pointer(31,90,0); assert(p.aim < -1.47); p.pointer(389,90,0); assert(p.aim>1.47);
    p.aim=0; p.key('q'); assert(std::abs(p.aim+.006)<1e-9);
    p.pegs={{210,200,true},{245,200,true},{210,235,false},{340,360,true}};
    p.orange_total=3; p.pegs[0].power=Peggle::Power::blast;
    p.hit(0); assert(p.pegs[1].hit && p.pegs[2].hit && !p.pegs[3].hit);
    int score=p.score; p.hit(0); assert(p.score==score); assert(p.multiplier()==5);
    p.reset(random); p.award(16000); assert(p.balls==13 && p.free_tier==3);
    p.award(1); assert(p.balls==13);
    p.shoot(); assert(p.shot_score==0 && p.free_tier==0 && p.balls==12);
    p.pegs={{150,200,false}}; p.pegs[0].power=Peggle::Power::catch_ball; p.hit(0);
    assert(p.shot_catch && p.catch_shots==3 && p.bucket_half()==61);
    p.pegs={{150,200,false}}; p.pegs[0].power=Peggle::Power::guide; p.hit(0); assert(p.guide_shots==3);
    std::vector<Peggle::Peg> brick{{210,210,false,false,false,0,20,5}};
    double x=210,y=198,dx=0,dy=315;
    Peggle::integrate(x,y,dx,dy,.01,brick); assert(dy<0 && y<200);
    brick[0].angle=.5; x=210;y=199;dx=0;dy=315;
    Peggle::integrate(x,y,dx,dy,.01,brick); assert(std::abs(dx)>100 && dy<0);
    for(int i=0;i<20;++i) {
        Game g(Kind::peggle,i); g.focused=true; g.arcade.aim=(i%9-4)*.35; g.key(' ');
        for(int frame=0;frame<1500 && !g.over;++frame) g.tick(1./60);
        assert(!g.arcade.flying && std::isfinite(g.arcade.ballX) && std::isfinite(g.arcade.vy));
        g.focused=false; auto before=g.arcade.clock; auto particles=g.arcade.particles; auto labels=g.arcade.labels;
        g.tick(1); g.pointer(40,100,1); g.key('r');
        assert(g.arcade.clock==before && g.arcade.particles.size()==particles.size() && g.arcade.labels.size()==labels.size());
    }
    p.reset(random); p.pegs={{210,200,true,true,false}}; p.balls=2;p.finish_shot(true);
    assert(p.over && p.won && p.balls==3 && p.score==1500);
    std::cout<<"Peggle layouts, bricks, powers, scores, free balls, aiming, physics and paused VFX passed.\n";
}
