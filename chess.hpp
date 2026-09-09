#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
namespace kalwer::games {
namespace chess {
enum Piece {pawn=1,knight,bishop,rook,queen,king};
struct Move {int from=-1,to=-1,promotion=0;bool operator==(const Move&)const=default;};
struct Position {
    std::array<int,64> board{};
    int side=1,rights=15,ep=-1,halfmove=0,ply=0;
    static Position initial(){Position p;const int back[8]={rook,knight,bishop,queen,king,bishop,knight,rook};for(int x=0;x<8;x++){p.board[x]=-back[x];p.board[8+x]=-pawn;p.board[48+x]=pawn;p.board[56+x]=back[x];}return p;}
    int king_square(int color)const{auto it=std::find(board.begin(),board.end(),color*king);return it==board.end()?-1:int(it-board.begin());}
    bool attacked(int square,int by)const {
        if(square<0 || square>=64)return true;
        int x=square%8,y=square/8;
        for(int i=0;i<64;i++)if(board[i]*by>0){
            int dx=x-i%8,dy=y-i/8,t=std::abs(board[i]);
            if(t==pawn && dy==-by && std::abs(dx)==1)return true;
            if(t==knight && std::abs(dx)*std::abs(dy)==2)return true;
            if(t==king && std::max(std::abs(dx),std::abs(dy))==1)return true;
            bool line=(t==bishop && std::abs(dx)==std::abs(dy)) || (t==rook && (dx==0 || dy==0)) || (t==queen && (dx==0 || dy==0 || std::abs(dx)==std::abs(dy)));
            if(!line || (!dx && !dy))continue;
            int sx=(dx>0)-(dx<0),sy=(dy>0)-(dy<0),cx=i%8+sx,cy=i/8+sy;bool clear=true;
            while(cx!=x || cy!=y){if(board[cy*8+cx]){clear=false;break;}cx+=sx;cy+=sy;}
            if(clear)return true;
        }
        return false;
    }
    bool check()const{return attacked(king_square(side),-side);}
    Position moved(Move m)const {
        Position p=*this;int piece=board[m.from],target=board[m.to];p.ep=-1;++p.ply;
        p.halfmove=(std::abs(piece)==pawn || target)?0:halfmove+1;
        p.board[m.to]=m.promotion?side*m.promotion:piece;p.board[m.from]=0;
        if(std::abs(piece)==pawn){if(m.to==ep && target==0)p.board[m.to+8*side]=0;if(std::abs(m.to-m.from)==16)p.ep=(m.to+m.from)/2;}
        if(std::abs(piece)==king){p.rights&=side==1?~3:~12;if(std::abs(m.to-m.from)==2){int rf=m.to>m.from?m.from+3:m.from-4,rt=m.to>m.from?m.from+1:m.from-1;p.board[rt]=p.board[rf];p.board[rf]=0;}}
        for(auto [square,bit]:std::array<std::pair<int,int>,4>{{{63,1},{56,2},{7,4},{0,8}}})if(m.from==square || m.to==square)p.rights&=~bit;
        p.side=-side;return p;
    }
    std::vector<Move> legal()const {
        std::vector<Move> moves;moves.reserve(48);
        auto add=[&](int from,int to){
            if(board[to]*side>0 || std::abs(board[to])==king)return;
            if(std::abs(board[from])==pawn && (to/8==0 || to/8==7))for(int t:{queen,rook,bishop,knight})moves.push_back({from,to,t});
            else moves.push_back({from,to,0});
        };
        constexpr int directions[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}};
        for(int i=0;i<64;i++)if(board[i]*side>0){
            int x=i%8,y=i/8,t=std::abs(board[i]);
            if(t==pawn){int ny=y-side;if(ny>=0 && ny<8){if(!board[ny*8+x]){add(i,ny*8+x);if(y==(side==1?6:1) && !board[(y-2*side)*8+x])add(i,(y-2*side)*8+x);}for(int dx:{-1,1})if(x+dx>=0 && x+dx<8){int to=ny*8+x+dx;if(board[to]*side<0 || (to==ep && board[to+8*side]==-side*pawn))add(i,to);}}continue;}
            if(t==knight){for(int dx:{-2,-1,1,2})for(int dy:{-2,-1,1,2})if(std::abs(dx)*std::abs(dy)==2 && x+dx>=0 && x+dx<8 && y+dy>=0 && y+dy<8)add(i,(y+dy)*8+x+dx);continue;}
            for(int d=0;d<8;d++){
                if((t==bishop && d<4) || (t==rook && d>=4))continue;
                int nx=x+directions[d][0],ny=y+directions[d][1];
                while(nx>=0 && nx<8 && ny>=0 && ny<8){int to=ny*8+nx;add(i,to);if(board[to] || t==king)break;nx+=directions[d][0];ny+=directions[d][1];}
            }
            if(t==king && i==(side==1?60:4) && !check()){
                int kbit=side==1?1:4,qbit=side==1?2:8;
                if((rights&kbit) && board[i+3]==side*rook && !board[i+1] && !board[i+2] && !attacked(i+1,-side) && !attacked(i+2,-side))moves.push_back({i,i+2,0});
                if((rights&qbit) && board[i-4]==side*rook && !board[i-1] && !board[i-2] && !board[i-3] && !attacked(i-1,-side) && !attacked(i-2,-side))moves.push_back({i,i-2,0});
            }
        }
        std::erase_if(moves,[&](Move m){auto p=moved(m);return p.attacked(p.king_square(side),-side);});return moves;
    }
    std::uint64_t key()const {
        std::uint64_t h=1469598103934665603ULL;
        for(int v:board){h^=std::uint64_t(v+7);h*=1099511628211ULL;}
        h^=std::uint64_t(rights+16*(side+1));h*=1099511628211ULL;
        // En-passant matters for repetition only when a legal capture exists.
        int effective=-1;if(ep>=0)for(auto m:legal())if(m.to==ep && std::abs(board[m.from])==pawn && m.from%8!=ep%8)effective=ep;
        return (h^std::uint64_t(effective+1))*1099511628211ULL;
    }
    bool insufficient()const {
        int minors=0,knights=0,bishop_color=-1;bool mixed=false;
        for(int i=0;i<64;i++)if(board[i] && std::abs(board[i])!=king){int t=std::abs(board[i]);if(t==pawn || t==rook || t==queen)return false;++minors;if(t==knight)++knights;else {int c=(i/8+i%8)%2;if(bishop_color>=0 && bishop_color!=c)mixed=true;bishop_color=c;}}
        return minors<=1 || (!knights && !mixed);
    }
};
inline int evaluate(const Position& p){
    constexpr int values[]={0,100,320,335,500,900,0};int sum=0;
    for(int i=0;i<64;i++)if(p.board[i]){int side=p.board[i]>0?1:-1,t=std::abs(p.board[i]);int central=6-std::abs(2*(i%8)-7)/2-std::abs(2*(i/8)-7)/2;int advance=side==1?6-i/8:i/8-1;sum+=side*(values[t]+(t==pawn?advance*8:t==knight || t==bishop?central*7:0));}
    return p.side*sum;
}
inline int search(const Position& p,int depth,int alpha,int beta,int& budget,int distance=0,
                  std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::time_point::max()){
    if(--budget<=0)return evaluate(p);
    if((budget&3)==0 && std::chrono::steady_clock::now()>=deadline){budget=0;return evaluate(p);}
    auto moves=p.legal();if(moves.empty())return p.check()?-30000+distance:0;
    if(p.halfmove>=100 || p.insufficient())return 0;
    if(depth<=0)return evaluate(p);
    std::stable_sort(moves.begin(),moves.end(),[&](Move a,Move b){return std::abs(p.board[a.to])*10+a.promotion>std::abs(p.board[b.to])*10+b.promotion;});
    int best=-32000;
    for(auto m:moves){int value=-search(p.moved(m),depth-1,-beta,-alpha,budget,distance+1,deadline);best=std::max(best,value);alpha=std::max(alpha,value);if(alpha>=beta || budget<=0)break;}
    return best;
}
}
struct Chess {
    chess::Position position=chess::Position::initial();
    std::vector<chess::Move> moves=position.legal();
    std::vector<std::uint64_t> history{position.key()};
    std::vector<chess::Move> pending_promotion,ai_moves;
    chess::Move last{},best{};
    bool over=false,won=false,started=false,local=false,wood=false;
    int selected=-1,cursor=52,ai_index=0,best_score=-32001;
    double ai_wait=0;
    std::string result,notice="White: you   Black: computer";
    void reset(bool two_players=false,bool walnut=false){*this=Chess();local=two_players;wood=walnut;notice=local?"Two players on this board":"White: you   Black: computer";}
    bool thinking()const{return !local && position.side==-1 && !over;}
    bool choose_allowed()const{return !over && !thinking();}
    void finish_move(chess::Move m){
        position=position.moved(m);last=m;started=true;selected=-1;pending_promotion.clear();moves=position.legal();history.push_back(position.key());
        if(moves.empty()){over=true;if(position.check()){won=local || position.side==-1;result=position.side==1?"Black wins by checkmate":"White wins by checkmate";}else result="Draw: stalemate";}
        else if(position.halfmove>=100){over=true;result="Draw: 50-move rule";}
        else if(std::count(history.begin(),history.end(),history.back())>=3){over=true;result="Draw: threefold repetition";}
        else if(position.insufficient()){over=true;result="Draw: insufficient material";}
        ai_moves.clear();ai_index=0;best_score=-32001;ai_wait=0;
        notice=position.check()?"CHECK - protect your king":local?(position.side==1?"White to move":"Black to move"):position.side==1?"Your move":"Computer is thinking...";
    }
    void choose(int square){
        if(!choose_allowed() || square<0 || square>=64 || !pending_promotion.empty())return;
        cursor=square;
        if(selected>=0){
            std::vector<chess::Move> targets;for(auto m:moves)if(m.from==selected && m.to==square)targets.push_back(m);
            if(!targets.empty()){if(targets[0].promotion){pending_promotion=targets;notice="Promote: Q queen, R rook, B bishop, N knight";}else finish_move(targets[0]);return;}
        }
        selected=position.board[square]*position.side>0?square:-1;
    }
    void promote(int type){for(auto m:pending_promotion)if(m.promotion==type){finish_move(m);return;}}
    void key(int key){
        if(!pending_promotion.empty()){if(key=='q' || key=='1')promote(chess::queen);if(key=='r' || key=='2')promote(chess::rook);if(key=='b' || key=='3')promote(chess::bishop);if(key=='n' || key=='4')promote(chess::knight);return;}
        if(key=='h'){reset(!local,wood);return;}
        if(!choose_allowed())return;
        if(key==1 || key=='a')cursor=cursor/8*8+(cursor%8+7)%8;
        if(key==2 || key=='d')cursor=cursor/8*8+(cursor%8+1)%8;
        if(key==3 || key=='w')cursor=(cursor+56)%64;
        if(key==4 || key=='s')cursor=(cursor+8)%64;
        if(key==13 || key==' ')choose(cursor);
    }
    void pointer(double x,double y,int button){
        if(button!=1)return;
        if(!pending_promotion.empty()){if(x>=70 && x<350 && y>=228 && y<280){constexpr int types[]={chess::queen,chess::rook,chess::bishop,chess::knight};promote(types[int((x-70)/70)]);}return;}
        if(y>=420 && y<445 && x<250){reset(!local,wood);return;}
        if(x>=50 && x<370 && y>=96 && y<416)choose(int((y-96)/40)*8+int((x-50)/40));
    }
    void advance(double dt){
        if(!thinking())return;
        ai_wait+=dt;if(ai_wait<.22)return;
        if(ai_moves.empty()){ai_moves=moves;std::stable_sort(ai_moves.begin(),ai_moves.end(),[&](auto a,auto b){return std::abs(position.board[a.to])>std::abs(position.board[b.to]);});}
        // One bounded root branch per focused tick: never launch an unbounded UI-thread search.
        if(ai_index<int(ai_moves.size())){
            int budget=1200;auto move=ai_moves[ai_index++];
            auto deadline=std::chrono::steady_clock::now()+std::chrono::microseconds(1500);
            int value=-chess::search(position.moved(move),2,-32000,32000,budget,0,deadline);
            if(value>best_score){best_score=value;best=move;}
        }
        if(ai_index==int(ai_moves.size()) && !ai_moves.empty())finish_move(best);
    }
    template<class P>static void piece(P& p,double x,double y,int type,unsigned c,unsigned ink){
        p.line(x-11,y+13,x+11,y+13,c,5);p.line(x-8,y+8,x+8,y+8,c,4);
        if(type==chess::pawn){p.line(x,y+6,x,y-2,c,9);p.circle(x,y-9,6,c);}
        if(type==chess::rook){p.rect(x-8,y-7,16,14,c);p.rect(x-11,y-12,22,6,c);for(int i=-1;i<=1;i++)p.rect(x+i*8-3,y-17,6,6,c);}
        if(type==chess::knight){p.line(x-4,y+5,x+4,y-7,c,12);p.line(x+4,y-7,x-6,y-12,c,10);p.line(x-6,y-12,x-11,y-5,c,7);p.line(x+3,y-11,x+1,y-18,c,5);p.circle(x-4,y-12,1.7,ink);}
        if(type==chess::bishop){p.line(x,y+7,x,y-6,c,10);p.circle(x,y-10,7,c);p.line(x-3,y-14,x+3,y-8,ink,2);p.circle(x,y-19,2,c);}
        if(type==chess::queen){p.line(x,y+6,x,y-5,c,12);for(int i=-1;i<=1;i++){p.line(x+i*5,y-4,x+i*10,y-14,c,4);p.circle(x+i*10,y-16,3,c);}}
        if(type==chess::king){p.line(x,y+6,x,y-8,c,12);p.circle(x,y-10,6,c);p.line(x,y-22,x,y-12,c,3);p.line(x-5,y-18,x+5,y-18,c,3);}
    }
    template<class P>void draw(P& p)const {
        p.text(24,55,14,over?result:notice,0xe0f5e8);
        p.text(24,76,11,local?"LOCAL - no koins awarded":"COMPUTER - win earns 60 koins",0x8dada1);
        bool in_check=position.check();
        for(int i=0;i<64;i++){
            double x=50+i%8*40,y=96+i/8*40;bool light=(i/8+i%8)%2==0;
            unsigned color=wood?(light?0xba9670:0x715541):(light?0xadc3b1:0x3a6255);
            if(i==last.from || i==last.to)color=wood?0xd4b372:0x719d72;
            if(i==selected)color=0xcfbc6d;
            if(position.board[i]==position.side*chess::king && in_check)color=0xcc7569;
            p.rect(x,y,40,40,color);
            if(position.board[i])piece(p,x+20,y+22,std::abs(position.board[i]),position.board[i]>0?0xfff0cf:0x202a33,position.board[i]>0?0x51483c:0xc4ddd1);
            if(i==cursor){p.line(x+2,y+2,x+37,y+2,0xffd579,2);p.line(x+2,y+2,x+2,y+37,0xffd579,2);}
        }
        if(selected>=0)for(auto m:moves)if(m.from==selected){double x=70+m.to%8*40,y=116+m.to/8*40;if(!position.board[m.to])p.circle(x,y,4,0x365744);else {p.line(x-17,y-17,x-7,y-17,0xffa657,3);p.line(x-17,y-17,x-17,y-7,0xffa657,3);}}
        for(int i=0;i<8;i++){p.text(38,121+i*40,10,std::to_string(8-i),0x8dada1);p.text(67+i*40,93,10,std::string(1,char('a'+i)),0x8dada1);}
        p.text(24,437,11,local?"H / click: play computer":"H / click: two players",0x8ce9b3);
        if(!pending_promotion.empty()){
            p.rect(63,211,294,82,0x112e29);p.text(74,226,12,"CHOOSE PROMOTION",0xe0f5e8);
            constexpr int types[]={chess::queen,chess::rook,chess::bishop,chess::knight};const char* labels[]={"Q","R","B","N"};
            for(int i=0;i<4;i++){piece(p,103+i*70,260,types[i],0xfff0cf,0x51483c);p.text(126+i*70,269,11,labels[i],0x8ce9b3);}
        }
    }
};
}
