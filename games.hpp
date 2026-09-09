#pragma once
#include "peggle.hpp"
#include "appearance.hpp"
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
struct Game {
    Kind kind;
    std::mt19937 random;
    bool focused = false, over = false, won = false, started = false;
    double elapsed = 0, accumulator = 0;
    int score = 0;
    bool reward_claimed=false;
    int reward=0;
    double celebration=0;
    std::deque<Cell> snake;
    Cell direction{1,0}, next{1,0}, food{};
    std::array<int,81> mines{};
    std::array<bool,81> revealed{}, flagged{};
    int cursor = 40;
    Peggle arcade;
    explicit Game(Kind k, unsigned seed = std::random_device{}()) : kind(k), random(seed) { reset(); }
    void reset() {
        over = won = started = false; reward_claimed=false; reward=0; celebration=0; elapsed = accumulator = 0; score = 0;
        snake = {{8,8},{7,8},{6,8}}; direction = next = {1,0}; spawn_food();
        mines.fill(0); revealed.fill(false); flagged.fill(false); cursor = 40;
        arcade.reset(random);
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
    void sync_arcade() { score=arcade.score; started=arcade.started; over=arcade.over; won=arcade.won; }
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
            arcade.key(key); sync_arcade();
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
        if(kind==Kind::peggle) { arcade.pointer(x,y,button); sync_arcade(); }
    }
    void tick(double dt) {
        if(!focused || dt<=0) return;
        if(over && won) {
            if(!reward_claimed) {
                int amount=kind==Kind::peggle?50+arcade.balls*5:kind==Kind::minesweeper?30:75;
                if(!kalwer::wallet.path.empty() && kalwer::wallet.credit(amount)) { reward_claimed=true; reward=amount; }
            }
            celebration+=std::min(dt,.05);
        }
        if(kind==Kind::peggle) {
            dt=std::min(dt,0.05);
            if(started && !over) elapsed+=dt;
            arcade.advance(dt); sync_arcade(); return;
        }
        if(over || !started) return;
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
        }
    }
    template<class P> void draw(P& p, bool embedded = false) const {
        constexpr unsigned bg=0x081a18, panel=0x112e29, green=0x8ce9b3, text=0xe0f5e8, muted=0x8dada1, orange=0xffa657;
        p.rect(0,0,420,490,bg);
        p.rect(0,38,420,40,0x0a211b);
        p.rect(0,447,420,43,0x0a211b);
        if(!embedded) { p.line(386,17,398,29,muted,2); p.line(398,17,386,29,muted,2); }
        if(!embedded) p.text(24,29,22,kind==Kind::snake?"SNAKE":kind==Kind::minesweeper?"MINESWEEPER":"PEGGLE",text);
        std::string status=kind==Kind::minesweeper?"Flags "+std::to_string(std::count(flagged.begin(),flagged.end(),true))+" / 10": "Score "+std::to_string(score);
        if(kind!=Kind::peggle) status+="   Time "+std::to_string(int(elapsed))+"s";
        if(kind!=Kind::peggle) p.text(24,56,14,status,muted);
        p.rect(30,80,360,360,panel);
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
        } else { arcade.draw(p); }

        if(kind!=Kind::peggle) p.text(24,467,12,kind==Kind::snake?"Arrows / WASD   Space: start":kind==Kind::minesweeper?"Click: reveal   Right click / F: flag":"Mouse / arrows   Click / Space: fire",muted);
        p.rect(318,449,78,28,0x2b5145); p.text(325,467,12,kind==Kind::peggle?"New R":"Restart R",text);
        if(won && focused && celebration<2.5) {
            for(int i=0;i<24;++i) {
                double t=celebration, a=i*2.39996;
                p.circle(210+std::cos(a)*(30+t*55),248+std::sin(a)*(25+t*40)+t*t*18,1.5,i%2?green:orange);
            }
        }
        if(!focused || over || !started) {
            p.rect(51,230,318,64,0x0a211b);
            p.text(66,257,20,!focused?"PAUSED - focus to resume":over?(won?"YOU WIN!":"GAME OVER"):"Ready when you are",green);
            p.text(66,281,12,over?(won && reward_claimed?"+"+std::to_string(reward)+" koins!  Wallet "+std::to_string(kalwer::wallet.balance):"Press R or click Restart"):kind==Kind::snake?"Press an arrow or Space to start":kind==Kind::minesweeper?"Reveal a square to start":"Orange: targets  Green: powers  Purple: bonus",text);
        }
    }
};
}
