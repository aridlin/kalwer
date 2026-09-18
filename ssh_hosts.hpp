#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace kalwer::ssh {
namespace fs=std::filesystem;
inline std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> out;std::string word;char quote=0;bool escape=false;
    for(char c:line) {
        if(escape){word+=c;escape=false;continue;}
        if(c=='\\'){escape=true;continue;}
        if(quote){if(c==quote)quote=0;else word+=c;continue;}
        if(c=='\''||c=='"'){quote=c;continue;}
        if(c=='#')break;
        if(c==' '||c=='\t'||c=='\r'||(c=='='&&out.empty())) {if(!word.empty()){out.push_back(word);word.clear();}}
        else word+=c;
    }
    if(!word.empty())out.push_back(word);
    return out;
}
inline bool pattern(const std::string& p,const std::string& s) {
    size_t i=0,j=0,star=std::string::npos,mark=0;
    while(j<s.size()) {
        if(i<p.size()&&(p[i]=='?'||p[i]==s[j])){++i;++j;}
        else if(i<p.size()&&p[i]=='*'){star=i++;mark=j;}
        else if(star!=std::string::npos){i=star+1;j=++mark;}
        else return false;
    }
    while(i<p.size()&&p[i]=='*')++i;
    return i==p.size();
}
inline fs::path from_utf8(const std::string& s) {return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()),s.size()));}
inline bool safe_alias(const std::string& s) {
    if(s.empty()||s.front()=='-')return false;
    return std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.';});
}
inline std::vector<std::string> hosts(const fs::path& home) {
    std::set<fs::path> seen;std::set<std::string> names;
    auto read=[&](auto&& self,const fs::path& path,int depth)->void {
        if(depth>16||seen.size()>=128)return;
        std::error_code ec;auto canonical=fs::weakly_canonical(path,ec);
        if(ec||!seen.insert(canonical).second||fs::file_size(path,ec)>1024*1024||ec)return;
        std::ifstream file(path);std::string line;
        while(std::getline(file,line)) {
            auto w=words(line);if(w.empty())continue;
            for(auto& c:w[0])if(c>='A'&&c<='Z')c+=32;
            if(w[0]=="host")for(size_t i=1;i<w.size();++i){if(safe_alias(w[i]))names.insert(w[i]);}
            if(w[0]!="include")continue;
            for(size_t i=1;i<w.size();++i) {
                fs::path include=from_utf8(w[i]);
                if(w[i].starts_with("~/"))include=home/from_utf8(w[i].substr(2));
                else if(include.is_relative())include=home/".ssh"/include;
                // Expand each path component without invoking a shell.
                std::vector<fs::path> paths{include.root_path()};
                for(const auto& part:include.relative_path()) {
                    std::vector<fs::path> next;const auto token=part.string();
                    for(const auto& base:paths) {
                        if(token.find_first_of("*?")==std::string::npos)next.push_back(base/part);
                        else for(fs::directory_iterator it(base,ec),end;!ec&&it!=end;it.increment(ec))
                            if(pattern(token,it->path().filename().string()))next.push_back(it->path());
                    }
                    paths=std::move(next);
                    if(paths.size()>128){paths.resize(128);}
                }
                std::sort(paths.begin(),paths.end());for(const auto& p:paths)self(self,p,depth+1);
            }
        }
    };
    read(read,home/".ssh"/"config",0);return {names.begin(),names.end()};
}
} // namespace kalwer::ssh
