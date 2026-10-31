#include "../notes_client.hpp"
#include <cassert>
#include <iostream>
int main(int argc,char** argv) {
    using namespace kalwer::notes;
    assert(parse("regular app").name.empty());
    assert(parse("*").name=="help");
    auto c=parse("*someday  żółw\nsecond line ");assert(c.save && c.text=="żółw\nsecond line");
    assert(parse("*someday").listing);assert(parse("*read query").listing);
    assert(parse("*read query").text=="query");
    assert(parse("*readable text").save && parse("*readable text").text=="readable text");
    assert(parse("*task").listing && parse("*idea").listing);
    for(auto input:{"*someday quote \" \\ żółw\nline","*task buy milk","*idea project","*disposable temporary"}) {
        auto parsed=parse(input);auto data=payload(parsed);auto p=json_parser_new();
        assert(json_parser_load_from_data(p,data.c_str(),-1,nullptr));
        auto n=decode_note(json_node_get_object(json_parser_get_root(p)));
        assert(n.body.find(parsed.text)!=std::string::npos);
        if(parsed.name=="someday")assert(n.lifecycle=="someday");
        if(parsed.name=="task")assert(n.type=="checklist");
        if(parsed.name=="idea")assert(n.type=="project");
        g_object_unref(p);
    }
    if(argc>1) {
        Client client;auto mode=std::string(argv[1]);auto r=client.run(parse("*read"));
        if(mode=="auth")assert(r.auth && !r.ok);
        else if(mode=="offline" || mode=="invalid")assert(!r.ok && !r.error.empty());
        else {
            assert(r.ok && r.notes.size()==1 && r.notes[0].body=="żółw");
            r=client.run(parse("*someday test"));assert(r.ok && r.notes.size()==1);
            r=client.run({"login",""},"tester","secret \"\\\n");assert(r.ok);
            r=client.run({"logout",""});assert(r.ok);
        }
    }
    std::cout<<"Notes parser, Unicode JSON, content decoding and client checks passed\n";
}
