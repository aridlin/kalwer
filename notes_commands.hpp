#pragma once
#include <string>
#include <array>
#include <algorithm>
namespace kalwer::notes {
struct Command { std::string name, text; bool listing=false, save=false; };
inline std::string trim(std::string s) {
    auto a=s.find_first_not_of(" \t\r\n");
    return a==s.npos?"":s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
inline Command parse(const std::string& input) {
    if(!input.starts_with('*'))return {};
    auto raw=trim(input.substr(1));auto end=raw.find_first_of(" \t\r\n");
    auto name=raw.substr(0,end);auto text=end==raw.npos?"":trim(raw.substr(end));
    if(name.empty() || name=="help")return {"help", ""};
    if(name=="read" || name=="search")return {"read",text,true};
    if(name=="login" || name=="logout" || name=="sync" || name=="web")return {name,text};
    for(auto word:{"note","someday","soon","reference","disposable","task","idea"})
        if(name==word)return {name,text,text.empty(),!text.empty()};
    return {"note",raw,false,true};
}
inline constexpr const char* help=
    "*note text — save a note (or just *text)\n"
    "*someday text — save for someday\n*soon text — save for soon\n"
    "*reference text — keep as reference\n*disposable text — temporary note\n"
    "*task text — checklist task\n*idea text — project idea\n"
    "*read — list notes; *read words — search\n"
    "*someday / *soon / *task / *idea — filtered list\n"
    "*sync — refresh from notes.aridlin.pl\n*login / *logout — account\n"
    "*web — open notes.aridlin.pl\n\n"
    "Enter saves; choosing a note opens its full text.\n"
    "Online saves are confirmed by the server. Failed saves stay in the input.\n";
}
