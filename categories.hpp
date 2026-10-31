#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include <glib.h>
namespace kalwer::categories {
struct App { std::string id, name, tags, icon; };
struct Category { std::string name, tag; bool automatic=true; std::vector<std::string> order{}; };
inline std::string lower(std::string s) { auto* p=g_utf8_casefold(s.c_str(),-1);s=p;g_free(p);return s; }
inline bool valid_name(const std::string& s) {
    return !s.empty() && s!="manage" && s.size()<=48 && std::all_of(s.begin(),s.end(),[](unsigned char c){return g_ascii_isalnum(c)||c=='-'||c=='_';});
}
inline std::vector<Category> defaults() { return {{"games","Game"},{"internet","Network"},{"office","Office"},{"graphics","Graphics"},{"development","Development"},{"multimedia","AudioVideo"},{"system","System"},{"utilities","Utility"},{"education","Education"}}; }
inline bool tagged(const App& app,const std::string& tag) { return !tag.empty() && (";"+app.tags).find(";"+tag+";")!=std::string::npos; }
inline std::vector<App> members(const Category& c,const std::vector<App>& apps,const std::string& query={}) {
    std::vector<App> out;
    auto add=[&](const App& a){if(lower(a.name+" "+a.id).find(lower(query))!=std::string::npos)out.push_back(a);};
    // Explicit order wins in both modes; automatic mode appends newly discovered apps.
    for(const auto& id:c.order) {auto i=std::find_if(apps.begin(),apps.end(),[&](const auto& a){return a.id==id;});if(i!=apps.end())add(*i);}
    if(c.automatic) {
        std::vector<App> rest;
        for(const auto& a:apps)if(tagged(a,c.tag)&&std::find(c.order.begin(),c.order.end(),a.id)==c.order.end())rest.push_back(a);
        std::stable_sort(rest.begin(),rest.end(),[](const auto& a,const auto& b){return lower(a.name)<lower(b.name);});
        for(const auto& a:rest)add(a);
    }
    return out;
}
inline std::vector<Category> load(const std::string& path) {
    auto out=defaults();auto* key=g_key_file_new();
    if(g_key_file_load_from_file(key,path.c_str(),G_KEY_FILE_NONE,nullptr)) {
        gsize n=0;auto** groups=g_key_file_get_groups(key,&n);
        for(gsize i=0;i<n;++i) {
            std::string name=groups[i];if(!valid_name(name))continue;
            auto it=std::find_if(out.begin(),out.end(),[&](const auto& c){return c.name==name;});
            if(it==out.end()){out.push_back({name,"",false,{}});it=out.end()-1;}
            it->automatic=g_key_file_get_boolean(key,groups[i],"automatic",nullptr);
            if(auto* tag=g_key_file_get_string(key,groups[i],"tag",nullptr)){it->tag=tag;g_free(tag);}
            gsize count=0;auto** ids=g_key_file_get_string_list(key,groups[i],"apps",&count,nullptr);it->order.clear();
            for(gsize j=0;j<count;++j)if(std::find(it->order.begin(),it->order.end(),ids[j])==it->order.end())it->order.emplace_back(ids[j]);
            g_strfreev(ids);
        }
        g_strfreev(groups);
    }
    g_key_file_unref(key);return out;
}
inline bool save(const std::string& path,const std::vector<Category>& cats,GError** error) {
    auto* key=g_key_file_new();
    for(const auto& c:cats) {
        g_key_file_set_boolean(key,c.name.c_str(),"automatic",c.automatic);
        g_key_file_set_string(key,c.name.c_str(),"tag",c.tag.c_str());
        std::vector<const char*> ids;for(const auto& id:c.order)ids.push_back(id.c_str());
        g_key_file_set_string_list(key,c.name.c_str(),"apps",ids.data(),ids.size());
    }
    auto* dir=g_path_get_dirname(path.c_str());g_mkdir_with_parents(dir,0700);g_free(dir);
    gsize n=0;auto* data=g_key_file_to_data(key,&n,nullptr);
    bool ok=g_file_set_contents_full(path.c_str(),data,n,G_FILE_SET_CONTENTS_CONSISTENT,0600,error);
    g_free(data);g_key_file_unref(key);return ok;
}
}
