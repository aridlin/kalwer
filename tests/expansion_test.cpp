#include "../games.hpp"
#include "../calculator_format.hpp"
#include "../passive_koins.hpp"
#include <cassert>
#include <iostream>
int main(){
 using namespace kalwer; using namespace kalwer::games;
 auto dir=std::filesystem::temp_directory_path()/"kalwer-expansion-test";
 std::filesystem::create_directories(dir);wallet.path=dir/"wallet";wallet.balance=100000;wallet.owned=wallet.equipped=0;
 assert(wallet.purchase(5,6000));assert(wallet.purchase(6,8000));
 Game t(Kind::tetris,42);t.key(' ');assert(!t.started);t.focused=true;t.key(' ');assert(t.started);t.focused=false;auto b=t.tetris.board;auto score=t.score;t.tick(5);t.key(' ');assert(t.tetris.board==b && t.score==score);
 Tetris rules;rules.reset(1);rules.board.fill(0);for(int r=16;r<20;r++)for(int x=0;x<10;x++)if(x!=4)rules.board[r*10+x]=1;
 rules.piece=0;rules.rotation=1;rules.x=2;rules.y=16;assert(rules.fits(2,16,1));rules.lock();assert(rules.lines==4 && rules.score==800);
 Game g(Kind::breakout,1);g.focused=true;g.key(' ');assert(g.breakout.flying);g.pointer(350,460,1);assert(!g.breakout.started && !g.over);
 g.breakout.score=300;g.breakout.over=g.over=true;auto balance=wallet.balance;g.tick(.01);g.tick(.01);assert(wallet.balance==balance+10 && g.reward==10);
 g.focused=false;auto x=g.breakout.ball_x;g.tick(1);assert(g.breakout.ball_x==x);
 assert(calculator::grouped("-1234567.123456") == "-1'234'567.123456");assert(calculator::grouped("1.234e-123") == "1.234e-123");assert(calculator::grouped("12345 / 6789") == "12'345 / 6'789");assert(calculator::scientific(123456789000000.) == "1.23456789e14");assert(calculator::scientific(1e-14) == "1e-14");assert(calculator::number(1e-20)!="0");
 balance=wallet.balance;calculator_completed("1000+20");calculator_completed("1000+20");assert(wallet.balance==balance+1);
 Shop shop;shop.selected=6;balance=wallet.balance;shop.pointer(50,390,1);assert(shop.selected==6 && wallet.balance==balance);
 std::ofstream(dir/"bad.wad",std::ios::binary)<<"IWAD";assert(!koom::valid_wad(dir/"bad.wad"));assert(koom::valid_wad("assets/koom/freedoom2.wad"));
 std::filesystem::remove_all(dir);std::cout<<"Expansion rules, rewards, focus, calculator and WAD validation passed.\n";
}
