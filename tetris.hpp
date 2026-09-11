#pragma once
#include <array>
#include <algorithm>
#include <random>
#include <string>
namespace kalwer::games {
struct Tetris {
    std::array<int,200> board{};
    std::array<int,7> bag{};int bag_pos=7;
    std::mt19937 random;
    int piece=0,next=0,rotation=0,x=3,y=0,score=0,lines=0;
    bool started=false,over=false,won=false;double clock=0;
    static constexpr unsigned colors[]={0x8bdcff,0xffd579,0xef9fe8,0x8ce9b3,0xff8179,0x839fff,0xffa657};
    static constexpr const char* shapes[]={"....XXXX........",".XX..XX.........",".X..XXX.........",".XX.XX..........","XX...XX.........","X...XXX.........","..X.XXX........."};
    bool cell(int p,int r,int cx,int cy)const {
        if(p==1)return shapes[p][cy*4+cx]=='X';
        int size=p==0?4:3;
        if(cx>=size || cy>=size)return false;
        for(int i=0;i<r;i++){int old=cx;cx=cy;cy=size-1-old;}
        return shapes[p][cy*4+cx]=='X';
    }
    bool fits(int nx,int ny,int nr)const {
        for(int cy=0;cy<4;cy++)for(int cx=0;cx<4;cx++)if(cell(piece,nr,cx,cy)) {
            int xx=nx+cx,yy=ny+cy;
            if(xx<0 || xx>=10 || yy>=20 || (yy>=0 && board[yy*10+xx]))return false;
        }return true;
    }
    int take(){if(bag_pos==7){for(int i=0;i<7;i++)bag[i]=i;std::shuffle(bag.begin(),bag.end(),random);bag_pos=0;}return bag[bag_pos++];}
    void spawn(){piece=next;next=take();rotation=0;x=3;y=0;clock=0;if(!fits(x,y,rotation))over=true;}
    void reset(unsigned seed){random.seed(seed);board.fill(0);bag_pos=7;score=lines=0;clock=0;started=over=won=false;next=take();spawn();}
    void lock(){
        for(int cy=0;cy<4;cy++)for(int cx=0;cx<4;cx++)if(cell(piece,rotation,cx,cy)) {if(y+cy<0){over=true;return;}board[(y+cy)*10+x+cx]=piece+1;}
        int cleared=0;
        for(int row=19;row>=0;--row){bool full=true;for(int c=0;c<10;c++)full&=board[row*10+c]!=0;if(!full)continue;
            for(int r=row;r>0;--r)for(int c=0;c<10;c++)board[r*10+c]=board[(r-1)*10+c];
            std::fill_n(board.begin(),10,0);cleared++;row++;
        }
        const int points[]={0,100,300,500,800};score+=points[cleared]*(1+lines/10);lines+=cleared;
        if(lines>=40){over=won=true;return;}spawn();
    }
    void key(int k){if(over)return;started=true;
        if(k==1 || k=='a'){if(fits(x-1,y,rotation))x--;}
        if(k==2 || k=='d'){if(fits(x+1,y,rotation))x++;}
        if(k==4 || k=='s'){if(fits(x,y+1,rotation)){y++;score++;}else lock();}
        if(k==3 || k=='w' || k=='x' || k=='z'){int r=(rotation+(k=='z'?3:1))%4;for(int dx:{0,-1,1,-2,2})if(fits(x+dx,y,r)){x+=dx;rotation=r;break;}}
        if(k==' '){while(fits(x,y+1,rotation)){y++;score+=2;}lock();}
    }
    void advance(double dt){if(!started || over)return;clock+=dt;double step=std::max(.08,.7-.13*(lines/10));while(clock>=step && !over){clock-=step;if(fits(x,y+1,rotation))y++;else lock();}}
    template<class P>void draw(P& p)const {
        p.rect(24,42,220,400,0x112e29);
        for(int row=0;row<20;row++)for(int col=0;col<10;col++)if(int c=board[row*10+col])p.rect(25+col*22,43+row*20,20,18,colors[c-1]);
        int ghost=y;while(fits(x,ghost+1,rotation))ghost++;
        for(int cy=0;cy<4;cy++)for(int cx=0;cx<4;cx++)if(cell(piece,rotation,cx,cy)) {
            p.rect(25+(x+cx)*22,43+(ghost+cy)*20,20,18,0x2b5145);
            p.rect(25+(x+cx)*22,43+(y+cy)*20,20,18,colors[piece]);
        }
        p.text(266,70,18,"NEXT",0x8dada1);
        for(int cy=0;cy<4;cy++)for(int cx=0;cx<4;cx++)if(cell(next,0,cx,cy))p.rect(265+cx*24,90+cy*24,22,22,colors[next]);
        p.text(266,215,16,"Score "+std::to_string(score),0xe0f5e8);
        p.text(266,248,16,"Lines "+std::to_string(lines)+"/40",0x8ce9b3);
        p.text(266,281,16,"Level "+std::to_string(1+lines/10),0xffd579);
        p.text(263,337,11,"Up / X: rotate",0x8dada1);p.text(263,360,11,"Z: rotate back",0x8dada1);
        p.text(263,383,11,"Space: hard drop",0x8dada1);p.text(24,466,11,"Arrows / WASD   Space: drop",0x8dada1);
    }
};
}
