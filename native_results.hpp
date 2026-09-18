#pragma once
#include "unicode_search.hpp"
#include "emoticons.hpp"
#include "ssh_hosts.hpp"
#include <optional>
namespace kalwer::native {
enum class Action { Copy, Ssh, Info };
struct Result {std::string title, subtitle, payload;Action action=Action::Copy;std::string preview{};};
inline bool command(const std::string& input,const std::string& cmd) {
    return input==cmd || (input.starts_with(cmd)&&input.size()>cmd.size()&&(input[cmd.size()]==' '||input[cmd.size()]=='\t'));
}
inline std::string tail(const std::string& input,size_t n) {
    auto s=input.substr(n);auto first=s.find_first_not_of(" \t");return first==std::string::npos?"":s.substr(first);
}
inline std::optional<std::vector<Result>> search(const std::string& input,const std::filesystem::path& home) {
    std::vector<Result> rows;
    if(input.starts_with("+")) {
        for(const auto& m:unicode::search(input))rows.push_back({unicode::label(m.code)+" · "+m.name,"Enter to copy character",m.text,Action::Copy,unicode::preview(m.code)});
        if(rows.empty())rows.push_back({"Unicode lookup","Type +200b or +zero; Enter copies the selected character",{},Action::Info});
    } else if(command(input,"/wemote")) {
        for(const auto& e:emoticons::search(tail(input,7)))rows.push_back({e.face,std::string(e.name)+" · "+e.category+" · Enter to copy",e.face});
        if(rows.empty())rows.push_back({"No matching emoticons","Search a name, category or tag after /wemote",{},Action::Info});
    } else if(command(input,"/wssh")) {
        const auto q=emoticons::lower(tail(input,5));
        for(const auto& h:ssh::hosts(home))if(emoticons::lower(h).find(q)!=std::string::npos)
            rows.push_back({h,"SSH config host · Enter to connect",h,Action::Ssh});
        if(rows.empty())rows.push_back({"No matching SSH hosts","Add a concrete Host alias to ~/.ssh/config (Include files supported)",{},Action::Info});
    } else return std::nullopt;
    return rows;
}
}
