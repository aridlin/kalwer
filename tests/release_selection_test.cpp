#include "../release_selection.hpp"
#include <cassert>
#include <iostream>
using namespace kalwer::updates;
int main(){
 assert(newer("0.10.0","0.9.9"));assert(newer("1.2.3","1.2.3-rc.9"));assert(!newer("1.2.3-rc.9","1.2.3"));
 assert(newer("1.2.3-rc.10","1.2.3-rc.9"));assert(newer("1.2.3-beta","1.2.3-alpha"));assert(!newer("1.2.3+build.2","1.2.3+build.1"));
 assert(!Version::parse("1.2.3+../../escape"));assert(!Version::parse("1.2.3+"));assert(!Version::parse("1.2.3-01"));assert(!Version::parse("1.2.9999999999999999999999999"));
 auto releases=Reader(R"([
 {"tag_name":"v0.9.2","draft":false,"prerelease":false,"body":"notes with \"tag_name\":\"v999.0.0\" and [braces] {}","assets":[{"name":"kalwer.exe"},{"name":"kalwer.exe.sha256"}]},
 {"tag_name":"v0.9.3-rc.1","draft":false,"prerelease":true,"author":{"id":42,"extra":[null,false,{"x":"escaped \\ and \u0031"}]},"assets":[{"name":"kalwer.exe"},{"name":"kalwer.exe.sha256"}]},
 {"tag_name":"v5.0.0","draft":true,"prerelease":false,"assets":[{"name":"kalwer.exe"},{"name":"kalwer.exe.sha256"}]},
 {"tag_name":"v9.0.0","draft":false,"prerelease":true,"assets":[{"name":"kalwer.exe"}]}
 ])").read();
 assert(releases && releases->size()==4);
 assert(select(*releases,"0.9.1","kalwer.exe",false)->tag=="v0.9.2");
 assert(select(*releases,"0.9.1","kalwer.exe",true)->tag=="v0.9.3-rc.1");
 assert(!select(*releases,"0.9.3","kalwer.exe",true));assert(!select(*releases,"0.9.1","kalwer-linux-x86_64",true));
 assert(!Reader("[] trailing").read());assert(!Reader("[{\"draft\":null}]").read());assert(!Reader("[{\"tag_name\":\"unfinished}]").read());assert(!Reader("{\"message\":\"rate limited\"}").read());
 auto escaped=Reader("[{\"tag_name\":\"v0.9.\\u0033\",\"prerelease\":true}]").read();assert(escaped && escaped->at(0).tag=="v0.9.3");
 std::cout<<"Release parsing, prerelease opt-in, version ordering, incomplete assets and downgrade protection passed.\n";
}
