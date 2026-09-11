#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
namespace kalwer::games {
struct Breakout {
    struct Brick{double x,y;int hp;};std::vector<Brick> bricks;
    double paddle=210,ball_x=210,ball_y=390,vx=105,vy=-240,time=0,flash=0;
    int score=0,board=0,lives=3;bool started=false,over=false,won=false,flying=false;
    bool wide=false,spare=false;
    double paddle_half()const{return wide?50.:40.;}
    void layout(){bricks.clear();for(int row=0;row<6;row++)for(int col=0;col<9;col++){
        if(board==1 && (row+col)%3==0)continue;
        if(board==2 && std::abs(col-4)>row+1)continue;
        bricks.push_back({24.+col*42,85.+row*24,board==2 && row<3?2:1});
    }flying=false;ball_y=390;ball_x=paddle;vx=105+board*25;vy=-240-board*35;}
    void reset(unsigned){paddle=210;score=board=0;lives=spare?4:3;time=flash=0;started=over=won=false;layout();}
    void key(int k){if(over)return;started=true;if(k==1 || k=='a')paddle=std::max(18.+paddle_half(),paddle-25);if(k==2 || k=='d')paddle=std::min(402.-paddle_half(),paddle+25);if(k==' ' || k==13)flying=true;}
    void pointer(double x,double,int button){if(over)return;paddle=std::clamp(x,18.+paddle_half(),402.-paddle_half());started=true;if(button==1)flying=true;}
    void advance(double dt){if(!started || over)return;time+=dt;flash=std::max(0.,flash-dt);if(!flying){ball_x=paddle;return;}
        for(int step=0;step<8;step++){double h=dt/8,ox=ball_x,oy=ball_y;ball_x+=vx*h;ball_y+=vy*h;
            if(ball_x<22){ball_x=22;vx=std::abs(vx);}if(ball_x>398){ball_x=398;vx=-std::abs(vx);}if(ball_y<48){ball_y=48;vy=std::abs(vy);}
            if(vy>0 && oy<=397 && ball_y>=397 && std::abs(ball_x-paddle)<=paddle_half()+4){double angle=std::clamp((ball_x-paddle)/(paddle_half()+4),-.95,.95)*1.12;double speed=std::min(430.,270.+board*45+score*.07);vx=std::sin(angle)*speed;vy=-std::cos(angle)*speed;ball_y=397;}
            for(auto& b:bricks)if(b.hp && ball_x+6>b.x && ball_x-6<b.x+38 && ball_y+6>b.y && ball_y-6<b.y+18){
                if(ox+6<=b.x || ox-6>=b.x+38)vx=-vx;else vy=-vy;
                ball_x=ox;ball_y=oy;b.hp--;score+=b.hp?5:20;flash=.08;break;
            }
            if(ball_y>442){lives--;flying=false;if(!lives)over=true;else{ball_x=paddle;ball_y=390;vy=-std::abs(vy);}break;}
        }
        if(std::none_of(bricks.begin(),bricks.end(),[](auto b){return b.hp>0;})){if(++board==3)over=won=true;else layout();}
    }
    template<class P>void draw(P& p)const {
        p.text(24,64,14,"Board "+std::to_string(board+1)+" / 3    Lives "+std::to_string(lives)+"    Score "+std::to_string(score),0xe0f5e8);
        for(auto b:bricks)if(b.hp)p.rect(b.x,b.y,38,18,b.hp>1?0xef9fe8:flash>0?0xe0f5e8:0x8ce9b3);
        p.rect(paddle-paddle_half(),404,paddle_half()*2,10,0x8bdcff);p.circle(ball_x,ball_y,6,0xffd579);
        if(!flying && !over)p.text(100,350,15,"Space / click to serve",0x8dada1);
        p.text(24,466,11,"Mouse or Left/Right: paddle   Space: serve",0x8dada1);
    }
};
}
