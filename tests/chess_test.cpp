#include "../games.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace kalwer::games;
using namespace kalwer::games::chess;
long perft(const Position& p,int depth){if(!depth)return 1;long count=0;for(auto m:p.legal())count+=perft(p.moved(m),depth-1);return count;}
bool contains(const Position& p,Move m){auto moves=p.legal();return std::find(moves.begin(),moves.end(),m)!=moves.end();}
void play(Chess& c,int from,int to){auto it=std::find_if(c.moves.begin(),c.moves.end(),[&](auto m){return m.from==from && m.to==to;});assert(it!=c.moves.end());c.finish_move(*it);}
int main(){
    auto initial=Position::initial();assert(perft(initial,1)==20);assert(perft(initial,2)==400);assert(perft(initial,3)==8902);
    Position castle;castle.rights=1;castle.board[60]=king;castle.board[63]=rook;castle.board[4]=-king;
    assert(contains(castle,{60,62,0}));auto castled=castle.moved({60,62,0});assert(castled.board[61]==rook && castled.board[63]==0 && !(castled.rights&3));
    castle.board[5]=-rook;assert(!contains(castle,{60,62,0}));
    Position ep;ep.rights=0;ep.board[60]=king;ep.board[0]=-king;ep.board[4]=-rook;ep.board[28]=pawn;ep.board[27]=-pawn;ep.ep=19;
    assert(!contains(ep,{28,19,0}));ep.board[4]=0;assert(contains(ep,{28,19,0}));auto captured=ep.moved({28,19,0});assert(captured.board[27]==0 && captured.board[19]==pawn && captured.ep==-1);
    Position promotion;promotion.rights=0;promotion.board[60]=king;promotion.board[7]=-king;promotion.board[8]=pawn;
    for(int type:{queen,rook,bishop,knight})assert(contains(promotion,{8,0,type}));
    Game ui(Kind::chess,1);ui.focused=true;ui.chess_game.position=promotion;ui.chess_game.moves=promotion.legal();ui.chess_game.history={promotion.key()};
    ui.chess_game.choose(8);ui.chess_game.choose(0);assert(ui.chess_game.pending_promotion.size()==4);ui.key('r');assert(ui.chess_game.position.board[0]==rook && ui.chess_game.position.ply==1);
    Chess fool;fool.local=true;play(fool,53,45);play(fool,12,28);play(fool,54,38);play(fool,3,39);assert(fool.over && fool.result=="Black wins by checkmate");
    Position stale;stale.rights=0;stale.side=-1;stale.board[0]=-king;stale.board[18]=king;stale.board[17]=queen;assert(stale.legal().empty() && !stale.check());
    Chess repeated;repeated.local=true;for(int i=0;i<2;i++){play(repeated,62,45);play(repeated,6,21);play(repeated,45,62);play(repeated,21,6);}assert(repeated.over && repeated.result=="Draw: threefold repetition");
    Position bare;bare.rights=0;bare.board[60]=king;bare.board[4]=-king;assert(bare.insufficient());bare.board[42]=bishop;assert(bare.insufficient());bare.board[43]=knight;assert(!bare.insufficient());
    Chess fifty;fifty.local=true;fifty.position.halfmove=99;play(fifty,62,45);assert(fifty.over && fifty.result=="Draw: 50-move rule");
    Game g(Kind::chess,4);g.focused=true;g.chess_game.choose(52);g.chess_game.choose(36);g.sync_extra();assert(g.chess_game.thinking());
    auto start=std::chrono::steady_clock::now();double worst=0;
    for(int i=0;i<200 && g.chess_game.thinking();i++){auto t=std::chrono::steady_clock::now();g.tick(.016);worst=std::max(worst,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count());}
    assert(g.chess_game.position.ply==2 && !g.chess_game.position.check());
    auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    g.chess_game.choose(62);g.chess_game.choose(45);g.sync_extra();g.tick(.05);g.focused=false;auto key=g.chess_game.position.key();int progress=g.chess_game.ai_index;for(int i=0;i<100;i++)g.tick(.05);assert(g.chess_game.position.key()==key && g.chess_game.ai_index==progress);
    std::cout<<"Chess perft, castling, en passant, promotion, mate, draws, legal AI and focus pause passed. AI "<<elapsed<<"ms total, worst tick "<<worst<<"ms\n";
}
