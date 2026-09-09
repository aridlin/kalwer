#include "../games.hpp"
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
    std::filesystem::remove_all(dir);std::cout<<"Legacy wallet migration, atomic purchases, ownership, equip toggles, effects and local chess reward exclusion passed.\n";
}
