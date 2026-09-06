#include "../system_file_index.hpp"
#include <cassert>
#include <iostream>
using namespace kalwer_files;
Reply await_reply(Index& index,unsigned long long id) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<deadline) {auto r=index.poll();if(r.request==id&&r.revision)return r;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    throw std::runtime_error("No search reply");
}
int main() {
    assert((search_terms("projects \"invoice 2026\" *.pdf")==std::vector<std::string>{"projects","invoice 2026","*.pdf"}));
    auto result=run_process({"printf","%s","a;$(false)\nname"},[]{return false;},std::chrono::seconds(1));
    assert(result.code==0&&result.out=="a;$(false)\nname");
    auto start=std::chrono::steady_clock::now();
    result=run_process({"sleep","10"},[]{return false;},std::chrono::milliseconds(50));
    assert(result.cancelled&&std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
    auto root=fs::temp_directory_path()/("kalwer-system-index-"+std::to_string(getpid()));
    fs::create_directories(root/"tree"/"projects");fs::create_directories(root/"index");
    std::ofstream(root/"tree"/"projects"/"invoice 2026.pdf")<<"test";
    std::ofstream(root/"tree"/"report.txt")<<"test";
    std::ofstream(root/"tree"/"multi\nline.txt")<<"test";
    std::ofstream(root/"tree"/from_utf8("zażółć.txt"))<<"test";
    result=run_process({"updatedb","-U",text(root/"tree"),"-o",text(root/"index"/"system.plocate"),"--prunepaths=","--prune-bind-mounts=no","--require-visibility=0"},[]{return false;},std::chrono::seconds(10));
    assert(result.code==0);
    {
        Index index(root/"index",false);
        auto reply=await_reply(index,index.request("port"));assert(reply.entries.size()==1&&reply.entries[0].name=="report.txt");
        reply=await_reply(index,index.request("projects \"invoice 2026\""));assert(reply.entries.size()==1&&reply.entries[0].name=="invoice 2026.pdf");
        reply=await_reply(index,index.request("multi"));assert(reply.entries.size()==1&&reply.entries[0].name=="multi\nline.txt");
        reply=await_reply(index,index.request("zażółć"));assert(reply.entries.size()==1);
        index.request("missing-name");reply=await_reply(index,index.request("report"));assert(reply.entries.size()==1);
    }
    fs::remove_all(root);
    std::cout<<"Passed system index substring, multi-term, Unicode, newline, cancellation and argv safety checks.\n";
}
