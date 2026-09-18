#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace kalwer::unicode {
struct Name { uint32_t code; uint16_t block, offset; };
#include "vendor/unicode/names.inc"
inline const char* name_text(const Name& n) { return name_blocks[n.block]+n.offset; }
struct Match { uint32_t code; std::string name, text; };
inline bool scalar(uint32_t c) { return c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff); }
inline std::string utf8(uint32_t c) {
    if (!scalar(c)) return {};
    std::string s;
    if(c<0x80) s+=char(c);
    else if(c<0x800) {s+=char(0xc0|(c>>6));s+=char(0x80|(c&63));}
    else if(c<0x10000) {s+=char(0xe0|(c>>12));s+=char(0x80|((c>>6)&63));s+=char(0x80|(c&63));}
    else {s+=char(0xf0|(c>>18));s+=char(0x80|((c>>12)&63));s+=char(0x80|((c>>6)&63));s+=char(0x80|(c&63));}
    return s;
}
inline std::string label(uint32_t c) { char s[16];std::snprintf(s,sizeof(s),"U+%04X",unsigned(c));return s; }
inline std::vector<Match> search(std::string query) {
    if(!query.empty() && query.front()=='+') query.erase(0,1);
    auto first=query.find_first_not_of(" \t\r\n"),last=query.find_last_not_of(" \t\r\n");
    query=first==std::string::npos?"":query.substr(first,last-first+1);
    for(char& c:query) if(c>='a'&&c<='z')c-=32;
    if(query.starts_with("U+") || query.starts_with("0X"))query.erase(0,2);
    if(query.empty()) return {};
    bool hex=query.size()<=6;uint32_t code=0;
    for(char c:query) {int n=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:-1;if(n<0){hex=false;break;}code=(code<<4)|n;}
    std::vector<Match> result;
    if(hex) {
        // NUL cannot be represented by the desktop text clipboard. Surrogates
        // are not Unicode scalar values. Do not offer either as copyable text.
        if(!scalar(code)||code==0) return result;
        auto it=std::lower_bound(std::begin(names),std::end(names),code,[](const Name& n,uint32_t c){return n.code<c;});
        std::string name=it!=std::end(names)&&it->code==code?name_text(*it):"UNNAMED CODE POINT";
        result.push_back({code,name,utf8(code)});return result;
    }
    for(const auto& n:names) if(std::string_view(name_text(n)).find(query)!=std::string_view::npos)
        result.push_back({n.code,name_text(n),utf8(n.code)});
    return result;
}
} // namespace kalwer::unicode
