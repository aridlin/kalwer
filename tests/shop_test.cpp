#include "../games.hpp"
#include "../launcher_commands.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace kalwer;
int main(){
    auto dir=std::filesystem::temp_directory_path()/("kalwer-shop-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));wallet=Wallet{};wallet.path=dir/"koins-v1";
    assert(atomic_text(wallet.path,"9000 7\n"));wallet.load();assert(wallet.balance==9000 && wallet.wins==7 && !wallet.owned);
    games::Shop shop;shop.selected=2;shop.activate();assert(wallet.balance==8000 && wallet.has(2) && wallet.uses(2) && wallet.wins==7);
    shop.activate();assert(wallet.balance==8000 && !wallet.uses(2));shop.activate();assert(wallet.balance==8000 && wallet.uses(2));
    Wallet restored;restored.path=wallet.path;restored.load();assert(restored.balance==8000 && restored.uses(2));
    shop.selected=1;shop.activate();assert(wallet.balance==500 && wallet.uses(1));shop.selected=3;shop.activate();assert(wallet.balance==500 && !wallet.has(3));
    assert(!wallet.purchase(-1,1) && !wallet.purchase(64,1) && !wallet.purchase(3,-5));
    games::Game garden(games::Kind::garden,1),chess(games::Kind::chess,1);assert(garden.garden.night && chess.chess_game.wood);
    assert(wallet.credit(4500));assert(wallet.has(1) && wallet.has(2));shop.selected=3;shop.activate();games::Game peggle(games::Kind::peggle,1);assert(peggle.arcade.layout==4 && peggle.arcade.pegs.size()>30);
    auto original=wallet.path;wallet.path=dir/"directory";std::filesystem::create_directories(wallet.path);auto balance=wallet.balance;auto owned=wallet.owned;assert(!wallet.credit(30) && wallet.balance==balance);shop.selected=2;shop.activate();assert(wallet.owned==owned && wallet.uses(2));wallet.path=original;
    games::Game local(games::Kind::chess,1);local.focused=true;local.chess_game.local=true;local.over=local.won=true;auto wins=wallet.wins;local.tick(.02);assert(wallet.wins==wins && wallet.balance==balance);
    // The hidden command is exact-only and absent from the public catalog.
    assert(matching_commands("/unlockall").size()==1 && matching_commands("/unlock").empty());
    assert(help().body.find("/unlockall")==std::string::npos);
    auto prior_balance=wallet.balance;auto prior_wins=wallet.wins;auto prior_owned=wallet.owned;
    assert(wallet.unlock_games() && wallet.balance==prior_balance && wallet.wins==prior_wins);
    assert(wallet.owned==(prior_owned|Wallet::game_mask));
    assert(wallet.unlock_games() && wallet.balance==prior_balance);
    assert(wallet.credit(200000,false));
    for(int id=9;id<Wallet::item_count;++id){shop.selected=id;shop.activate();assert(wallet.uses(id));}
    restored.load();assert(restored.owned==wallet.owned && restored.equipped==wallet.equipped);
    games::Game upgraded(games::Kind::tetris,42);
    assert(upgraded.tetris.hold_enabled && upgraded.tetris.slow_gravity);
    assert(upgraded.breakout.wide && upgraded.breakout.spare && upgraded.breakout.lives==4);
    assert(upgraded.kar.motion.nitro==75*games::Kar::unit && upgraded.kar.quick_recovery);
    assert(upgraded.koom.field_kit && upgraded.koom.bounty);
    auto& blocks=upgraded.tetris;int first=blocks.piece;blocks.key('c');assert(blocks.held==first);
    int second=blocks.piece;blocks.key('c');assert(blocks.piece==second && blocks.held==first);
    blocks.key(' ');blocks.key('c');assert(blocks.piece==first);
    games::Tetris normal,slow;normal.reset(42);slow.slow_gravity=true;slow.reset(42);normal.started=slow.started=true;
    normal.advance(.75);slow.advance(.75);assert(normal.y==1 && slow.y==0);
    games::Breakout narrow,wide;narrow.reset(42);wide.wide=true;wide.reset(42);
    for(auto* breaker:{&narrow,&wide}){breaker->started=breaker->flying=true;breaker->ball_x=258;breaker->ball_y=396;breaker->vx=0;breaker->vy=240;breaker->advance(.02);}
    assert(narrow.vy>0 && wide.vy<0);
    for(int id=9;id<Wallet::item_count;++id){shop.selected=id;auto cash=wallet.balance;shop.activate();assert(!wallet.uses(id) && wallet.has(id) && wallet.balance==cash);}
    upgraded.reset();assert(!upgraded.tetris.hold_enabled && !upgraded.tetris.slow_gravity && upgraded.breakout.lives==3 && !upgraded.breakout.wide);
    assert(upgraded.kar.motion.nitro==25*games::Kar::unit && !upgraded.kar.quick_recovery && !upgraded.koom.field_kit && !upgraded.koom.bounty);
    games::Kar standard,quick;standard.reset(42);quick.quick_recovery=true;quick.reset(42);
    standard.hit(games::Kar::Impact::Hit::knockdown,games::kar_physics::from_kph(200));
    quick.hit(games::Kar::Impact::Hit::knockdown,games::kar_physics::from_kph(200));
    assert(standard.phase==games::Kar::Phase::wreck && standard.phase_time==2 && quick.phase_time==1);
    shop.selected=0;shop.pointer(340,440,1);assert(shop.selected==6);shop.pointer(340,440,1);assert(shop.selected==12);shop.pointer(60,440,1);assert(shop.selected==6);
    auto before_passive=wallet.balance;assert(wallet.claim_passive(12) && wallet.balance==before_passive+12);
    assert(wallet.claim_passive(12) && wallet.balance==before_passive+12);
    restored.load();assert(restored.passive_claimed==12);assert(restored.claim_passive(15) && restored.balance==before_passive+15);
    // Failed hidden unlock saves must not change memory or grant ownership.
    wallet.owned=wallet.equipped=0;wallet.path=dir/"directory";assert(!wallet.unlock_games() && wallet.owned==0);wallet.path=original;
    std::filesystem::remove_all(dir);std::cout<<"Legacy wallet migration, atomic purchases, ownership, equip toggles, effects and local chess reward exclusion passed.\n";
}
