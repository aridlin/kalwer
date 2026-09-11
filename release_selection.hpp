#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kalwer::updates {
struct Version {
    std::array<unsigned,3> core{};
    std::vector<std::string> pre;
    static std::optional<Version> parse(std::string_view value){
        if(value.starts_with('v'))value.remove_prefix(1);
        if(value.empty() || value.size()>128)return {};
        for(char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='.'||c=='-'||c=='+'))return {};
        auto build=value.find('+');if(build!=value.npos){auto suffix=value.substr(build+1);if(suffix.empty() || suffix.front()=='.' || suffix.back()=='.' || suffix.find("..")!=suffix.npos || suffix.find('+')!=suffix.npos)return {};value=value.substr(0,build);}
        Version out;auto dash=value.find('-');auto numbers=value.substr(0,dash);
        for(int i=0;i<3;++i){auto dot=numbers.find('.');auto part=numbers.substr(0,dot);if(part.empty() || (part.size()>1 && part[0]=='0'))return {};
            auto [end,error]=std::from_chars(part.data(),part.data()+part.size(),out.core[i]);if(error!=std::errc{} || end!=part.data()+part.size())return {};
            if(i<2){if(dot==numbers.npos)return {};numbers.remove_prefix(dot+1);}else if(dot!=numbers.npos)return {};
        }
        if(dash!=value.npos){auto rest=value.substr(dash+1);for(;;){auto dot=rest.find('.');auto part=rest.substr(0,dot);if(part.empty())return {};
                bool numeric=true;for(char c:part){if(!((c>='0' && c<='9')||(c>='a' && c<='z')||(c>='A' && c<='Z')||c=='-'))return {};numeric&=c>='0' && c<='9';}
                if(numeric && part.size()>1 && part[0]=='0')return {};
                out.pre.emplace_back(part);
                if(dot==rest.npos)break;
                rest.remove_prefix(dot+1);
            }}return out;
    }
};
inline bool newer(std::string_view candidate,std::string_view current){
    auto a=Version::parse(candidate),b=Version::parse(current);if(!a || !b)return false;
    if(a->core!=b->core)return a->core>b->core;
    if(a->pre.empty() || b->pre.empty())return a->pre.empty() && !b->pre.empty();
    for(size_t i=0;i<std::min(a->pre.size(),b->pre.size());++i){const auto& x=a->pre[i];const auto& y=b->pre[i];if(x==y)continue;
        auto numeric=[](const std::string& s){return std::all_of(s.begin(),s.end(),[](char c){return c>='0' && c<='9';});};bool xn=numeric(x),yn=numeric(y);
        if(xn!=yn)return !xn;
        if(xn && x.size()!=y.size())return x.size()>y.size();
        return x>y;
    }return a->pre.size()>b->pre.size();
}
struct Release {std::string tag;bool draft=false,prerelease=false;std::vector<std::string> assets;};
// Read only release metadata. Unknown GitHub fields are skipped structurally,
// including nested author objects and escaped strings in release notes.
class Reader {
    std::string_view input;size_t at=0;bool valid=true;
    void ws(){while(at<input.size() && (input[at]==' '||input[at]=='\r'||input[at]=='\n'||input[at]=='\t'))++at;}
    bool take(char ch){ws();if(at<input.size() && input[at]==ch){++at;return true;}return false;}
    std::string string(){
        if(!take('"')){valid=false;return {};}std::string out;
        while(at<input.size()){unsigned char c=input[at++];if(c=='"')return out;if(c<32){valid=false;return {};}
            if(c=='\\'){if(at==input.size())break;c=input[at++];
                if(c=='u'){unsigned code=0;for(int i=0;i<4;++i){if(at==input.size()){valid=false;return {};}char h=input[at++];int d=h>='0'&&h<='9'?h-'0':h>='a'&&h<='f'?h-'a'+10:h>='A'&&h<='F'?h-'A'+10:-1;if(d<0){valid=false;return {};}code=code*16+unsigned(d);}out+=code<128?char(code):'?';continue;}
                if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c=='b')c='\b';else if(c=='f')c='\f';else if(c!='"' && c!='\\' && c!='/'){valid=false;return {};}
            }out+=char(c);
        }valid=false;return {};
    }
    bool boolean(){ws();if(input.substr(at,4)=="true"){at+=4;return true;}if(input.substr(at,5)=="false"){at+=5;return false;}valid=false;return false;}
    void skip(unsigned depth=0){
        ws();if(depth>64 || at==input.size()){valid=false;return;}
        if(input[at]=='"'){string();return;}
        if(take('{')){if(take('}'))return;do{string();if(!take(':')){valid=false;return;}skip(depth+1);}while(valid && take(','));if(!take('}'))valid=false;return;}
        if(take('[')){if(take(']'))return;do{skip(depth+1);}while(valid && take(','));if(!take(']'))valid=false;return;}
        for(auto literal:{std::string_view("null"),std::string_view("true"),std::string_view("false")})if(input.substr(at,literal.size())==literal){at+=literal.size();return;}
        size_t begin=at;while(at<input.size() && std::string_view("0123456789+-.eE").find(input[at])!=std::string_view::npos)++at;if(begin==at)valid=false;
    }
    void assets(Release& release){
        if(!take('[')){valid=false;return;}if(take(']'))return;
        do{if(!take('{')){valid=false;return;}if(!take('}')){do{auto key=string();if(!take(':')){valid=false;return;}if(key=="name")release.assets.push_back(string());else skip();}while(valid && take(','));if(!take('}'))valid=false;}}while(valid && take(','));if(!take(']'))valid=false;
    }
public:
    explicit Reader(std::string_view value):input(value){}
    std::optional<std::vector<Release>> read(){
        if(input.size()>8*1024*1024 || !take('['))return {};
        std::vector<Release> result;if(take(']')){ws();if(at!=input.size())return {};return result;}
        do{if(!take('{') || result.size()>=1000)return {};Release release;
            if(!take('}')){do{auto key=string();if(!take(':'))return {};if(key=="tag_name")release.tag=string();else if(key=="draft")release.draft=boolean();else if(key=="prerelease")release.prerelease=boolean();else if(key=="assets")assets(release);else skip();}while(valid && take(','));if(!take('}'))valid=false;}
            result.push_back(std::move(release));
        }while(valid && take(','));if(!take(']'))valid=false;ws();if(!valid || at!=input.size())return {};return result;
    }
};
inline std::optional<Release> select(const std::vector<Release>& releases,std::string_view current,std::string_view asset,bool include_prereleases){
    std::optional<Release> best;
    for(const auto& release:releases){
        if(release.draft || (!include_prereleases && release.prerelease) || !release.tag.starts_with('v') || !Version::parse(release.tag) || !newer(release.tag,current))continue;
        // Do not choose a newly-created release before its verified binary is uploaded.
        auto has=[&](std::string_view name){return std::find(release.assets.begin(),release.assets.end(),name)!=release.assets.end();};
        if(!has(asset) || !has(std::string(asset)+".sha256"))continue;
        if(!best || newer(release.tag,best->tag))best=release;
    }return best;
}
}
