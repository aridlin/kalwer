#include "../native_results.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
int main() {
    namespace n=kalwer::native;namespace u=kalwer::unicode;namespace fs=std::filesystem;
    for(const auto& entry:u::names) {
        assert(entry.block<std::size(u::name_blocks));
        assert(*u::name_text(entry));
    }
    auto z=u::search("+200c");assert(!z.empty()&&z[0].name=="ZERO WIDTH NON-JOINER"&&z[0].text=="\xe2\x80\x8c");
    z=u::search("+zero");assert(z.size()>10);assert(std::any_of(z.begin(),z.end(),[](auto& x){return x.code==0x200b;}));
    assert(u::search("+U+1f600")[0].text=="\xf0\x9f\x98\x80");
    assert(u::search("+D800").empty());assert(u::search("+110000").empty());assert(u::search("+0000").empty());
    assert(u::search("+garbage").empty());assert(u::search("+  200B ")[0].name=="ZERO WIDTH SPACE");
    z=u::search("+fe");assert(z.size()>1&&z.front().code==0xfe);
    assert(std::any_of(z.begin(),z.end(),[](const auto& x){return x.name=="FEMALE SIGN";}));
    assert(std::count_if(z.begin(),z.end(),[](const auto& x){return x.code==0xfe;})==1);
    assert(u::search("+U+FE").size()==1);
    assert(u::preview(0x41)=="A"&&u::preview(0x1f600)==u::utf8(0x1f600));
    assert(u::preview(0x200c)=="␣"&&u::preview(0x20)=="␣");
    assert(u::preview(0x301)=="◌́");
    const auto home=fs::temp_directory_path()/("kalwer-ssh-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(home/".ssh"/"hosts");
    {std::ofstream f(home/".ssh"/"config");f<<"Host alpha beta\n HostName example.test\nHost *.wild !excluded -bad bad;command\nInclude hosts/*.conf\nInclude config\n";}
    {std::ofstream f(home/".ssh"/"hosts"/"remote.conf");f<<"Host=remote\nHost \"quoted\" # comment\nHost alpha\n";}
    auto hosts=kalwer::ssh::hosts(home);assert((hosts==std::vector<std::string>{"alpha","beta","quoted","remote"}));
    assert(!n::search("ordinary app",home));assert(!n::search("/wsshevil",home));
    auto rows=n::search("/wssh REM",home);assert(rows->size()==1&&(*rows)[0].payload=="remote"&&(*rows)[0].action==n::Action::Ssh);
    rows=n::search("/wssh no-match",home);assert((*rows)[0].action==n::Action::Info);
    rows=n::search("/wemote shrug",home);assert(rows->size()==1&&(*rows)[0].payload=="¯\\_(ツ)_/¯"&&(*rows)[0].action==n::Action::Copy);
    rows=n::search("+200b",home);assert((*rows)[0].payload=="\xe2\x80\x8b"&&(*rows)[0].action==n::Action::Copy);
    rows=n::search("+fe",home);assert(rows->size()>1&&rows->front().preview=="þ"&&rows->front().payload=="þ");
    fs::remove_all(home);std::cout<<"PASS: Unicode scalars/names/UTF-8, native copy rows, emoticons, SSH includes/filtering and unsafe alias exclusion\n";
}
