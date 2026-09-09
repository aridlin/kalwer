#include "../games.hpp"
#include <cassert>
#include <iostream>
using namespace kalwer::games;
void advance(Game& g,double seconds) { for(int i=0;i<int(seconds*240);++i) g.tick(1./240); }
int main() {
    for(auto kind:{Kind::snake,Kind::minesweeper,Kind::peggle}) {
        Game g(kind,42); auto snake=g.snake;
        g.key(' '); g.pointer(210,250,1); advance(g,20);
        assert(!g.started && g.elapsed==0 && g.snake==snake);
        g.focused=true; g.key(' '); if(kind==Kind::minesweeper) g.reveal(40);
        advance(g,0.25); g.focused=false;
        auto elapsed=g.elapsed; auto x=g.ballX,y=g.ballY; snake=g.snake; auto flags=g.flagged; auto aim=g.aim;
        g.key('r'); g.key('f'); g.pointer(350,460,1); g.pointer(280,300,1); advance(g,50);
        assert(g.elapsed==elapsed && g.snake==snake && g.ballX==x && g.ballY==y && g.flagged==flags && g.aim==aim);
        g.focused=true; advance(g,0.1); assert(g.elapsed>elapsed);
    }
    Game s(Kind::snake,1); s.focused=true; s.key(1); assert(!s.started); // no reversal
    s.key(3); s.key(1); assert((s.next==Cell{0,-1})); // cannot queue a reversal within a step
    s.food={8,7}; advance(s,0.15); assert(s.score==1 && s.snake.size()==4);
    advance(s,5); assert(s.over && !s.won); s.key('r'); assert(!s.over && !s.started && s.score==0);
    s.snake={{1,1},{1,2},{0,2},{0,1}}; s.direction=s.next={-1,0}; s.food={10,10}; s.started=true;
    advance(s,0.15); assert(!s.over); // moving into the departing tail is legal
    for(unsigned seed=0;seed<100;++seed) for(int first:{0,40,80}) {
        Game m(Kind::minesweeper,seed); m.focused=true; m.reveal(first);
        assert(!m.over && m.mines[first]==0 && std::count(m.mines.begin(),m.mines.end(),-1)==10);
        for(int i=0;i<81;++i) if(m.mines[i]>=0) m.reveal(i);
        assert(m.won && m.score==71);
    }
    Game m(Kind::minesweeper,1); m.focused=true; m.key('f'); m.key(' '); assert(!m.started); m.key('f'); m.key(' ');
    int mine=int(std::find(m.mines.begin(),m.mines.end(),-1)-m.mines.begin()); m.reveal(mine); assert(m.over && !m.won);
    Game p(Kind::peggle,2); p.focused=true; p.key(' '); assert(p.flying && p.balls==9);
    p.key(' '); assert(p.balls==9); advance(p,20); assert(!p.flying && p.score>0);
    p.reset(); p.started=p.flying=true; p.balls=0; p.pegs={{210,210,true,true,false}}; p.ballY=428;
    p.physics(1./240); assert(p.won && p.over);
    p.reset(); p.started=p.flying=true; p.balls=0; p.ballX=35; p.ballY=428;
    p.physics(1./240); assert(p.over && !p.won);
    p.reset(); p.started=p.flying=true; p.balls=1; p.ballX=210; p.ballY=428;
    p.physics(1./240); assert(p.balls==2 && !p.over);
    for(int seed=0;seed<30;++seed) {
        Game g(Kind::peggle,seed); g.focused=true;
        for(int shot=0;shot<30 && !g.over;++shot) { g.aim=(seed%7-3)*0.3; g.key(' '); advance(g,19); assert(!g.flying && std::isfinite(g.ballX) && std::isfinite(g.ballY)); }
        assert(g.over);
    }
    std::cout<<"Game rules, focus pause, safe reveal, collisions and round completion passed.\n";
}
