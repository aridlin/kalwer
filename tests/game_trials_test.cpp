#include "../games.hpp"
#include <cassert>
#include <iostream>
int main(){
 using namespace kalwer;using namespace kalwer::games;
 auto dir=std::filesystem::temp_directory_path()/"kalwer-daily-trial-test";
 std::filesystem::remove_all(dir);wallet.path=dir/"wallet";wallet.balance=100000;wallet.owned=wallet.equipped=0;
 int date=20260911;game_trials=GameTrials{};game_trials.today=[&]{return date;};
 assert(game_trials.remaining(0)==600 && game_trials.remaining(3)==600);
 {
  Game game(Kind::tetris,42);game.focus(true);assert(!game.playable());
  game.tick(100);assert(game_trials.remaining(0)==600);
  game.key(13);assert(game.playable() && game.trial_active);
  // Starting a round consumes only this game's allowance.
  game.started=game.tetris.started=true;game.tick(1);
  assert(game_trials.remaining(0)==599 && game_trials.remaining(1)==600);
  game.focus(false);game.tick(100);assert(game_trials.remaining(0)==599);
  GameTrials reload;reload.today=[&]{return date;};assert(reload.remaining(0)==599);
  game.focus(true);game.key('r');assert(game_trials.remaining(0)==599);
  game_trials.consume(0,598.99);game.started=game.tetris.started=true;
  game.tick(.02);assert(!game.playable() && !game.trial_active && game_trials.remaining(0)==0);
  auto board=game.tetris.board;game.key(' ');game.tick(1);assert(board==game.tetris.board);
  assert(wallet.purchase(5,6000));assert(game.playable());game.tick(1);assert(game_trials.remaining(0)==0);
 }
 // Usage survives a fresh ledger, resets on a later local date, and clock
 // rollback does not produce another allowance.
 game_trials.flush();game_trials=GameTrials{};game_trials.today=[&]{return date;};
 assert(game_trials.remaining(0)==0 && game_trials.remaining(1)==600);
 ++date;assert(game_trials.remaining(0)==600);assert(game_trials.begin(0));game_trials.consume(0,30);game_trials.flush();
 --date;assert(game_trials.remaining(0)==570);
 for(int i=1;i<4;++i){assert(game_trials.begin(i));assert(game_trials.consume(i,601)==600);assert(game_trials.remaining(i)==0);}
 assert(game_trials.remaining(0)==570);
 // An unwritable ledger does not grant a trial that cannot be persisted.
 auto broken=dir/"not-a-directory";std::ofstream(broken)<<"file";wallet.path=broken/"wallet";
 assert(!game_trials.begin(0) && game_trials.save_failed);
 wallet.path.clear();game_trials=GameTrials{};std::filesystem::remove_all(dir);
 std::cout<<"Per-game daily trials: focus, restart persistence, expiry, purchase, date rollover and save failure passed.\n";
}
