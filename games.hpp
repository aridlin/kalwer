#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <random>
#include <string>
#include <vector>

namespace kalwer::games {
// All coordinates are in a 420 x 490 logical canvas. Hosts own focus and clocks.
enum class Kind { snake, minesweeper, peggle };
struct Cell { int x, y; bool operator==(const Cell&) const = default; };
struct Peg { double x, y; bool orange, hit = false, removed = false; };
struct Game {
    Kind kind;
    std::mt19937 random;
    bool focused = false, over = false, won = false, started = false;
    double elapsed = 0, accumulator = 0;
    int score = 0;
    std::deque<Cell> snake;
    Cell direction{1,0}, next{1,0}, food{};
    std::array<int,81> mines{};
    std::array<bool,81> revealed{}, flagged{};
    int cursor = 40;
    std::vector<Peg> pegs;
    double ballX = 210, ballY = 105, vx = 0, vy = 0, aim = 0, bucket = 210, flight = 0;
    bool flying = false;
    int balls = 10;
    explicit Game(Kind k, unsigned seed = std::random_device{}()) : kind(k), random(seed) { reset(); }
    void reset() {
        over = won = started = false; elapsed = accumulator = 0; score = 0;
        snake = {{8,8},{7,8},{6,8}}; direction = next = {1,0}; spawn_food();
        mines.fill(0); revealed.fill(false); flagged.fill(false); cursor = 40;
        pegs.clear();
        for (int y=0; y<7; ++y) for (int x=0; x<9; ++x)
            pegs.push_back({54.0+x*38+(y%2)*8, 162.0+y*32, (x+y*3)%4==0});
        ballX=210; ballY=105; vx=vy=aim=flight=0; bucket=210; flying=false; balls=10;
    }
    void spawn_food() {
        std::vector<Cell> free;
        for(int y=0;y<18;++y) for(int x=0;x<18;++x)
            if(std::find(snake.begin(),snake.end(),Cell{x,y})==snake.end()) free.push_back({x,y});
        if(free.empty()) { won=over=true; return; }
        food=free[random()%free.size()];
    }
    void plant(int safe) {
        std::vector<int> cells;
        for(int i=0;i<81;++i) if(std::abs(i%9-safe%9)>1 || std::abs(i/9-safe/9)>1) cells.push_back(i);
        std::shuffle(cells.begin(),cells.end(),random);
        for(int i=0;i<10;++i) mines[cells[i]]=-1;
        for(int i=0;i<81;++i) if(mines[i]!=-1)
            for(int j=0;j<81;++j) if(mines[j]==-1 && std::abs(i%9-j%9)<=1 && std::abs(i/9-j/9)<=1) ++mines[i];
    }
    void reveal(int cell) {
        if(over || flagged[cell]) return;
        if(!started) { plant(cell); started=true; }
        if(mines[cell]==-1) { revealed[cell]=true; over=true; return; }
        std::vector<int> pending{cell};
        while(!pending.empty()) {
            int i=pending.back(); pending.pop_back();
            if(revealed[i] || flagged[i]) continue;
            revealed[i]=true;
            if(mines[i]==0) for(int j=0;j<81;++j)
                if(!revealed[j] && std::abs(i%9-j%9)<=1 && std::abs(i/9-j/9)<=1) pending.push_back(j);
        }
        score=static_cast<int>(std::count(revealed.begin(),revealed.end(),true));
        if(score==71) over=won=true;
    }
    void shoot() {
        if(flying || over) return;
        started=flying=true; --balls; flight=0; ballX=210; ballY=105;
        vx=std::sin(aim)*260; vy=std::cos(aim)*260;
    }
    void key(int key) { // arrows: 1 left, 2 right, 3 up, 4 down; other keys ASCII
        if(!focused) return;
        if(key=='r' || key=='R') { reset(); return; }
        if(over) return;
        if(kind==Kind::snake) {
            Cell d=next;
            if(key==1 || key=='a') d={-1,0};
            if(key==2 || key=='d') d={1,0};
            if(key==3 || key=='w') d={0,-1};
            if(key==4 || key=='s') d={0,1};
            if((key>=1 && key<=4) || key=='a' || key=='d' || key=='w' || key=='s')
                if(!(d==Cell{-direction.x,-direction.y})) { next=d; started=true; }
            if(key==' ') started=true;
        } else if(kind==Kind::minesweeper) {
            if(key==1 || key=='a') cursor=cursor/9*9+(cursor%9+8)%9;
            if(key==2 || key=='d') cursor=cursor/9*9+(cursor%9+1)%9;
            if(key==3 || key=='w') cursor=(cursor+72)%81;
            if(key==4 || key=='s') cursor=(cursor+9)%81;
            if(key=='f' && !revealed[cursor]) flagged[cursor]=!flagged[cursor];
            if(key==' ' || key==13) reveal(cursor);
        } else {
            if(!flying && (key==1 || key=='a')) aim=std::max(-1.2,aim-0.06);
            if(!flying && (key==2 || key=='d')) aim=std::min(1.2,aim+0.06);
            if(key==' ' || key==13) shoot();
        }
    }
    void pointer(double x, double y, int button) {
        if(!focused) return;
        if(button==1 && y>=451 && x>=310) { reset(); return; }
        if(over) return;
        if(kind==Kind::minesweeper && x>=30 && x<390 && y>=80 && y<440) {
            cursor=int((y-80)/40)*9+int((x-30)/40);
            if(button==1) reveal(cursor);
            if(button==3 && !revealed[cursor]) flagged[cursor]=!flagged[cursor];
        }
        if(kind==Kind::peggle && !flying) {
            aim=std::clamp(std::atan2(x-210,std::max(25.0,y-105)),-1.2,1.2);
            if(button==1 && y>110 && y<440) shoot();
        }
    }
    void tick(double dt) {
        if(!focused || over || !started || dt<=0) return;
        dt=std::min(dt,0.05); elapsed+=dt;
        if(kind==Kind::snake) {
            accumulator+=dt;
            const double step=std::max(0.065,0.145-score*0.003);
            if(accumulator<step) return;
            accumulator-=step; direction=next;
            Cell head{snake.front().x+direction.x,snake.front().y+direction.y};
            bool eating=head==food;
            auto end=eating?snake.end():std::prev(snake.end());
            if(head.x<0 || head.x>=18 || head.y<0 || head.y>=18 || std::find(snake.begin(),end,head)!=end) { over=true; return; }
            snake.push_front(head);
            if(eating) { ++score; spawn_food(); } else snake.pop_back();
        } else if(kind==Kind::peggle) {
            // Fixed small physics steps keep collisions stable on slow frames.
            accumulator+=dt;
            while(accumulator>=1.0/240) { physics(1.0/240); accumulator-=1.0/240; }
        }
    }
    void physics(double dt) {
        bucket=210+135*std::sin(elapsed*1.3);
        if(!flying) return;
        flight+=dt; vy+=200*dt; ballX+=vx*dt; ballY+=vy*dt;
        if(ballX<35) { ballX=35; vx=std::abs(vx)*0.9; }
        if(ballX>385) { ballX=385; vx=-std::abs(vx)*0.9; }
        if(ballY<94) { ballY=94; vy=std::abs(vy); }
        for(auto& p:pegs) if(!p.removed) {
            double dx=ballX-p.x, dy=ballY-p.y, distance=std::hypot(dx,dy);
            if(distance<14) {
                if(!p.hit) { p.hit=true; score+=p.orange?100:10; }
                if(distance<0.001) { dx=0; dy=-1; distance=1; }
                dx/=distance; dy/=distance; ballX=p.x+dx*14; ballY=p.y+dy*14;
                double dot=vx*dx+vy*dy;
                if(dot<0) { vx-=1.85*dot*dx; vy-=1.85*dot*dy; }
            }
        }
        if(ballY>=427 || flight>18) {
            if(ballY>=427 && std::abs(ballX-bucket)<35) ++balls;
            flying=false;
            for(auto& p:pegs) if(p.hit) p.removed=true;
            won=std::none_of(pegs.begin(),pegs.end(),[](const Peg& p){return p.orange && !p.removed;});
            over=won || balls==0;
        }
    }
    template<class P> void draw(P& p, bool embedded = false) const {
        constexpr unsigned bg=0x081a18, panel=0x112e29, green=0x8ce9b3, text=0xe0f5e8, muted=0x8dada1, orange=0xffa657;
        p.rect(0,0,420,490,bg);
        if(!embedded) { p.line(386,17,398,29,muted,2); p.line(398,17,386,29,muted,2); }
        if(!embedded) p.text(24,29,22,kind==Kind::snake?"SNAKE":kind==Kind::minesweeper?"MINESWEEPER":"PEGGLE",text);
        std::string status=kind==Kind::minesweeper?"Flags "+std::to_string(std::count(flagged.begin(),flagged.end(),true))+" / 10": "Score "+std::to_string(score);
        if(kind==Kind::peggle) status+="   Balls "+std::to_string(balls);
        else status+="   Time "+std::to_string(int(elapsed))+"s";
        p.text(24,56,14,status,muted); p.rect(30,80,360,360,panel);
        if(kind==Kind::snake) {
            for(auto c:snake) p.rect(31+c.x*20,81+c.y*20,18,18,c==snake.front()?text:green);
            p.circle(40+food.x*20,90+food.y*20,7,orange);
        } else if(kind==Kind::minesweeper) {
            for(int i=0;i<81;++i) {
                double x=30+i%9*40,y=80+i/9*40;
                p.rect(x+2,y+2,36,36,revealed[i]?0x183b32:0x2b5145);
                if((revealed[i] || over) && mines[i]==-1) p.circle(x+20,y+20,8,orange);
                else if(revealed[i] && mines[i]) p.text(x+14,y+27,21,std::to_string(mines[i]),green);
                else if(flagged[i]) { p.line(x+14,y+10,x+14,y+31,text,2); p.line(x+15,y+11,x+28,y+16,orange,5); }
                if(i==cursor) { p.line(x+3,y+3,x+37,y+3,green,2); p.line(x+3,y+3,x+3,y+37,green,2); p.line(x+37,y+3,x+37,y+37,green,2); p.line(x+3,y+37,x+37,y+37,green,2); }
            }
        } else {
            p.circle(210,99,12,green);
            if(!flying) for(int i=1;i<=7;++i) p.circle(210+std::sin(aim)*i*10,105+std::cos(aim)*i*10,2,muted);
            for(const auto& peg:pegs) if(!peg.removed) p.circle(peg.x,peg.y,8,peg.hit?text:peg.orange?orange:0x67bce0);
            p.line(bucket-35,433,bucket+35,433,green,6);
            if(flying) p.circle(ballX,ballY,6,text);
        }
        p.text(24,467,12,kind==Kind::snake?"Arrows / WASD   Space: start":kind==Kind::minesweeper?"Click: reveal   Right click / F: flag":"Mouse / arrows   Click / Space: fire",muted);
        p.rect(318,449,78,28,0x2b5145); p.text(325,467,12,"Restart R",text);
        if(!focused || over || !started) {
            p.rect(51,230,318,64,bg);
            p.text(66,257,20,!focused?"PAUSED - focus to resume":over?(won?"YOU WIN!":"GAME OVER"):"Ready when you are",green);
            p.text(66,281,12,over?"Press R or click Restart":kind==Kind::snake?"Press an arrow or Space to start":kind==Kind::minesweeper?"Reveal a square to start":"Clear every orange peg in 10 balls",text);
        }
    }
};
}
