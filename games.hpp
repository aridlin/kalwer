#pragma once
#include "peggle.hpp"
#include "appearance.hpp"
#include "garden.hpp"
#include "chess.hpp"
#include "shop.hpp"
#include "tetris.hpp"
#include "breakout.hpp"
#include "koom.hpp"
#include "kar.hpp"
#include "game_trials.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <random>
#include <string>
#include <vector>

namespace kalwer::games {
// All coordinates are in a 420 x 490 logical canvas. Hosts own focus and clocks.
enum class Kind { snake, minesweeper, peggle, garden, chess, shop, tetris, breakout, kar, koom, catalog };
inline const char* name(Kind k){static const char* names[]={"Snake","Minesweeper","Peggle","Garden Defense","Chess","Koin Shop","Tetris","Breakout","Kar","Koom","Games"};return names[int(k)];}
inline bool is_command(const std::string& s){return s=="/snake" || s=="/minesweeper" || s=="/peggle" || s=="/pvz" || s=="/chess" || s=="/shop" || s=="/games" || s=="/tetris" || s=="/breakout" || s=="/kar" || s=="/koom" || s=="/doom";}
inline Kind command_kind(const std::string& s){return s=="/snake"?Kind::snake:s=="/minesweeper"?Kind::minesweeper:s=="/peggle"?Kind::peggle:s=="/pvz"?Kind::garden:s=="/chess"?Kind::chess:s=="/tetris"?Kind::tetris:s=="/breakout"?Kind::breakout:s=="/kar"?Kind::kar:(s=="/koom" || s=="/doom")?Kind::koom:s=="/games"?Kind::catalog:Kind::shop;}
inline int unlock_id(Kind k){return k==Kind::tetris?5:k==Kind::breakout?6:k==Kind::kar?7:k==Kind::koom?8:-1;}
inline bool unlocked(Kind k){return unlock_id(k)<0 || wallet.has(unlock_id(k));}
inline constexpr std::array<Kind,9> catalog_games{Kind::snake,Kind::minesweeper,Kind::peggle,Kind::garden,Kind::chess,Kind::tetris,Kind::breakout,Kind::kar,Kind::koom};
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
    Garden garden;
    Chess chess_game;
    Shop shop;
    Tetris tetris; Breakout breakout; Kar kar; kalwer::koom::Game koom; int catalog_selection=0;
    bool wrap=false,aurora=false;
    bool trial_active=false;
    explicit Game(Kind k, unsigned seed = std::random_device{}()) : kind(k), random(seed) { reset(); }
    ~Game(){game_trials.flush();}
    bool playable()const{return unlocked(kind) || (trial_active && game_trials.remaining(unlock_id(kind)-5)>0 && !game_trials.save_failed);}
    std::string title()const{return std::string(name(kind))+(!unlocked(kind) && trial_active?"  "+game_trials.label(unlock_id(kind)-5)+" trial":"");}
    bool trial_running()const {
        if(unlocked(kind) || !trial_active)return false;
        if(kind==Kind::koom){if(!koom.session)return false;std::lock_guard lock(koom.session->mutex);return koom.session->sequence>0 && koom.session->error.empty();}
        return started && !over;
    }
    void reset() {
        over = won = started = false; reward_claimed=false; reward=0; celebration=0; elapsed = accumulator = 0; score = 0;
        snake = {{8,8},{7,8},{6,8}}; direction = next = {1,0}; spawn_food();
        mines.fill(0); revealed.fill(false); flagged.fill(false); cursor = 40;
        wrap=wallet.uses(0);aurora=wallet.uses(4);
        arcade.reset(random,wallet.uses(3));garden.reset(random(),wallet.uses(1));chess_game.reset(chess_game.local,wallet.uses(2));
        tetris.reset(random());breakout.reset(random());kar.reset(random());if(kind==Kind::koom)koom.reset();
        if(kind==Kind::shop || kind==Kind::catalog)started=true;
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
    void sync_new(){if(kind==Kind::kar){score=kar.score;started=kar.started;over=kar.over;won=kar.won;}if(kind==Kind::tetris){score=tetris.score;started=tetris.started;over=tetris.over;won=tetris.won;}if(kind==Kind::breakout){score=breakout.score;started=breakout.started;over=breakout.over;won=breakout.won;}}
    void sync_extra(){if(kind==Kind::garden){score=garden.kills*10;started=garden.started;over=garden.over;won=garden.won;}if(kind==Kind::chess){score=chess_game.position.ply;started=chess_game.started;over=chess_game.over;won=chess_game.won;}}
    void focus(bool active){focused=active;if(!active)game_trials.flush();koom.focus(active && kind==Kind::koom && playable());kar.focus(active);}
    void release(int key){if(kind==Kind::koom)koom.key(key,false);if(kind==Kind::kar)kar.key(key,false);}
    void key(int key) { // arrows: 1 left, 2 right, 3 up, 4 down; other keys ASCII
        if(!focused) return;
        if(kind==Kind::catalog){
            if(key==3 || key=='w')catalog_selection=(catalog_selection+8)%9;
            if(key==4 || key=='s')catalog_selection=(catalog_selection+1)%9;
            if(key==13 || key==' '){kind=catalog_games[catalog_selection];reset();}return;
        }
        if(!playable()){
            if((key==13 || key==' ') && game_trials.begin(unlock_id(kind)-5))trial_active=true;
            else return;
        }
        if(kind==Kind::shop){shop.key(key);return;}
        if(kind==Kind::chess && !chess_game.pending_promotion.empty()){chess_game.key(key);sync_extra();return;}
        if(kind==Kind::chess && key=='h'){chess_game.local=!chess_game.local;reset();return;}
        if(key=='r' || key=='R') { reset(); return; }
        if(kind==Kind::koom){koom.key(key);return;}
        if(kind==Kind::kar){kar.key(key);sync_new();return;}
        if(over) return;
        if(kind==Kind::tetris){tetris.key(key);sync_new();return;}
        if(kind==Kind::breakout){breakout.key(key);sync_new();return;}
        if(kind==Kind::garden){garden.key(key);sync_extra();return;}
        if(kind==Kind::chess){chess_game.key(key);sync_extra();return;}
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
        if(kind==Kind::catalog){if(button==1 && x>=24 && x<396 && y>=55 && y<433){catalog_selection=int((y-55)/42);kind=catalog_games[catalog_selection];reset();}return;}
        if(!playable()){if(button==1 && y>=270 && y<315 && x>=35 && x<385)key(13);return;}
        if(kind==Kind::shop){shop.pointer(x,y,button);return;}
        if(button==1 && y>=451 && x>=310) { reset(); return; }
        if(kind==Kind::breakout){breakout.pointer(x,y,button);sync_new();return;}
        if(over) return;
        if(kind==Kind::garden){garden.pointer(x,y,button);sync_extra();return;}
        if(kind==Kind::chess){if(button==1 && y>=420 && y<445 && x<250 && chess_game.pending_promotion.empty()){chess_game.local=!chess_game.local;reset();return;}chess_game.pointer(x,y,button);sync_extra();return;}
        if(kind==Kind::minesweeper && x>=30 && x<390 && y>=80 && y<440) {
            cursor=int((y-80)/40)*9+int((x-30)/40);
            if(button==1) reveal(cursor);
            if(button==3 && !revealed[cursor]) flagged[cursor]=!flagged[cursor];
        }
        if(kind==Kind::peggle) { arcade.pointer(x,y,button); sync_arcade(); }
    }
    void tick(double dt) {
        if(focused && trial_running() && dt>0){
            dt=game_trials.consume(unlock_id(kind)-5,std::min(dt,1.));
            if(game_trials.remaining(unlock_id(kind)-5)<=0){trial_active=false;kar.focus(false);}
        }
        if(kind==Kind::koom){koom.focus(focused && playable());return;}
        if(!focused || dt<=0 || !playable()) return;
        if((kind==Kind::tetris || kind==Kind::breakout) && over && !reward_claimed){int amount=kind==Kind::tetris?std::min(250,tetris.lines*4+tetris.score/200):std::min(250,breakout.score/30+(won?80:0));if(amount==0)reward_claimed=true;else if(wallet.credit(amount,won)){reward_claimed=true;reward=amount;}}
        if(over && won) {
            if(!reward_claimed && kind!=Kind::tetris && kind!=Kind::breakout) {
                int amount=kind==Kind::peggle?50+arcade.balls*5:kind==Kind::minesweeper?30:kind==Kind::kar?std::min(250,100+(8-kar.position)*5+kar.score/1000):kind==Kind::garden?(garden.night?100:75):kind==Kind::chess?(chess_game.local?0:60):wrap?30:75;
                if(amount==0)reward_claimed=true;
                if(!kalwer::wallet.path.empty() && kalwer::wallet.credit(amount)) { reward_claimed=true; reward=amount; }
            }
            celebration+=std::min(dt,.05);
        }
        if(kind==Kind::shop || kind==Kind::catalog)return;
        if(kind==Kind::kar){kar.advance(dt);elapsed=kar.elapsed;sync_new();return;}
        if(kind==Kind::tetris || kind==Kind::breakout){dt=std::min(dt,.05);if(started && !over)elapsed+=dt;if(kind==Kind::tetris)tetris.advance(dt);else breakout.advance(dt);sync_new();return;}
        if(kind==Kind::garden || kind==Kind::chess){dt=std::min(dt,.05);if(started && !over)elapsed+=dt;if(kind==Kind::garden)garden.advance(dt);else chess_game.advance(dt);sync_extra();return;}
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
            if(wrap){head.x=(head.x+18)%18;head.y=(head.y+18)%18;}
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
        if(!embedded) p.text(24,29,22,name(kind),text);
        if(kind==Kind::catalog){for(int i=0;i<9;i++){auto k=catalog_games[i];double y=55+i*42;p.rect(24,y,372,38,i==catalog_selection?0x2b5145:0x112e29);p.text(34,y+25,16,name(k),text);p.text(225,y+25,12,unlocked(k)?"PLAY":"TRIAL "+game_trials.label(unlock_id(k)-5)+" / day",unlocked(k)?green:orange);}p.text(24,465,11,"Arrows + Enter or click a game",muted);return;}
        if(!playable()){
            int id=unlock_id(kind)-5;bool available=game_trials.remaining(id)>0;
            p.text(35,150,25,name(kind),green);
            p.text(35,190,17,available?"Daily trial: "+game_trials.label(id)+" remaining":"Today's trial is finished",orange);
            p.text(35,225,12,"10 minutes per game, every day. Pauses unfocused.",text);
            p.text(35,250,12,std::to_string(Shop::items[unlock_id(kind)].cost)+" koins in /shop for unlimited play",muted);
            if(available){p.rect(35,270,350,45,panel);p.text(55,298,15,"ENTER / SPACE / CLICK: PLAY TRIAL",green);}
            else p.text(35,298,14,"More trial time tomorrow. This popup stays open.",green);
            if(game_trials.save_failed)p.text(35,337,12,"Could not save trial time. Press Enter to retry.",orange);
            return;
        }
        if(kind==Kind::koom){koom.draw(p);return;}
        if(kind==Kind::kar){
            kar.draw(p);
            if(!focused){p.rect(52,208,316,48,0x0a211b);p.text(65,237,16,"PAUSED - focus to resume",green);}
            if(over && reward_claimed && reward>0)p.text(65,435,15,"+"+std::to_string(reward)+" koins!",orange);
            return;
        }
        if(kind==Kind::shop){shop.draw(p);return;}
        std::string status=kind==Kind::minesweeper?"Flags "+std::to_string(std::count(flagged.begin(),flagged.end(),true))+" / 10": "Score "+std::to_string(score);
        if(kind!=Kind::peggle) status+="   Time "+std::to_string(int(elapsed))+"s";
        if(kind==Kind::snake || kind==Kind::minesweeper) p.text(24,56,14,status,muted);
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
        } else if(kind==Kind::peggle){arcade.draw(p);}
        else if(kind==Kind::garden){garden.draw(p);}
        else if(kind==Kind::chess){chess_game.draw(p);}
        else if(kind==Kind::tetris){tetris.draw(p);}
        else if(kind==Kind::breakout){breakout.draw(p);}

        if(kind!=Kind::peggle && kind!=Kind::tetris && kind!=Kind::breakout) p.text(24,467,11,kind==Kind::snake?(wrap?"WRAP ARCADE - Arrows / WASD":"CLASSIC - Arrows / WASD"):kind==Kind::minesweeper?"Click: reveal   Right click / F: flag":kind==Kind::chess?"Click piece, then destination":"Arrows: bed   Enter: plant   1-5: seed",muted);
        p.rect(318,449,78,28,0x2b5145); p.text(325,467,12,kind==Kind::peggle?"New R":"Restart R",text);
        if(won && focused && celebration<2.5) {
            for(int i=0;i<24;++i) {
                double t=celebration, a=i*2.39996;
                p.circle(210+std::cos(a)*(30+t*55),248+std::sin(a)*(25+t*40)+t*t*18,1.5,aurora?(i%2?0xef9fe8:0x8bdcff):i%2?green:orange);
            }
        }
        if(!focused || over || (!started && kind!=Kind::chess)) {
            p.rect(51,230,318,64,0x0a211b);
            p.text(66,257,20,!focused?"PAUSED - focus to resume":over?(won?"YOU WIN!":"GAME OVER"):"Ready when you are",green);
            p.text(66,281,11,over?(reward_claimed && reward>0?"+"+std::to_string(reward)+" koins!  Wallet "+std::to_string(kalwer::wallet.balance):"Press R or click Restart"):kind==Kind::snake?"Press an arrow or Space to start":kind==Kind::minesweeper?"Reveal a square to start":kind==Kind::garden?"Plant a seed or press Space to begin":kind==Kind::chess?"Choose a piece and its destination":kind==Kind::tetris?"Arrows: move / rotate   Space: hard drop":kind==Kind::breakout?"Mouse / arrows: paddle   Space: serve":"Orange: targets  Green: powers  Purple: bonus",text);
        }
    }
};
}
