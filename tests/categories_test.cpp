#include "../categories.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace kalwer::categories;
int main() {
    std::vector<App> apps={{"b.desktop","Beta","Game;ArcadeGame;",""},{"a.desktop","Alpha","Game;",""},{"c.desktop","Żółw","Graphics;",""},{"d.desktop","Not a game","BoardGame;",""}};
    Category c{"games","Game",true,{}};
    assert(members(c,apps).size()==2 && members(c,apps)[0].id=="a.desktop");
    c.order={"b.desktop","missing.desktop"};assert(members(c,apps)[0].id=="b.desktop");
    c.automatic=false;assert(members(c,apps).size()==1);
    c.order={"c.desktop","b.desktop"};assert(members(c,apps,"ŻÓŁW").size()==1);
    assert(!valid_name("manage") && !valid_name("bad/name") && valid_name("xp-classics"));
    auto* tmp=g_dir_make_tmp("kalwer-categories-XXXXXX",nullptr);assert(tmp);std::string path=std::string(tmp)+"/nested/categories.ini";
    auto cats=defaults();cats.push_back({"xp-classics","",false,{"c.desktop","b.desktop"}});
    GError* error=nullptr;assert(save(path,cats,&error));assert(!error);
    auto restored=load(path);assert(restored.back().name=="xp-classics");assert(restored.back().order==cats.back().order);assert(!restored.back().automatic);
    assert(members(restored.back(),apps)[0].id=="c.desktop");
    assert(!save("/proc/kalwer-categories.ini",cats,&error));assert(error);g_clear_error(&error);
    std::filesystem::remove_all(tmp);g_free(tmp);
    std::cout<<"Automatic tags, manual ordering, missing apps, Unicode filtering, configuration round-trip and failed writes passed\n";
}
