#pragma once
#include "notes_commands.hpp"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <memory>
#include <stdexcept>
#include <vector>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
namespace kalwer::notes {
inline std::string field(JsonObject* o,const char* k) {
    if(!o || !json_object_has_member(o,k))return {};
    auto n=json_object_get_member(o,k);
    return JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n)==G_TYPE_STRING?json_node_get_string(n):"";
}
inline std::string quote(const std::string& s) {
    JsonNode* n=json_node_new(JSON_NODE_VALUE);json_node_set_string(n,s.c_str());
    gchar* encoded=json_to_string(n,FALSE);std::string out=encoded;g_free(encoded);json_node_free(n);return out;
}
struct Note {std::string id,title,body,type,lifecycle,status;};
inline Note decode_note(JsonObject* n) {
    Note out{field(n,"id"),field(n,"title"),"",field(n,"type"),field(n,"lifecycle"),field(n,"status")};
    auto content=json_object_get_member(n,"content");
    if(content && JSON_NODE_HOLDS_OBJECT(content)) {
        auto c=json_node_get_object(content);
        for(auto key:{"markdown","text","idea","next_action","caption","filename"}) {
            auto text=field(c,key);if(!text.empty()){if(!out.body.empty())out.body+="\n\n";out.body+=text;}
        }
        auto items=json_object_get_member(c,"items");
        if(items && JSON_NODE_HOLDS_ARRAY(items)) {
            auto list=json_node_get_array(items);
            for(guint i=0;i<json_array_get_length(list);++i) {
                auto node=json_array_get_element(list,i);if(!JSON_NODE_HOLDS_OBJECT(node))continue;
                auto item=json_node_get_object(node);
                bool done=json_object_has_member(item,"done") && json_object_get_boolean_member(item,"done");
                out.body+=(done?"[x] ":"[ ] ")+field(item,"text")+"\n";
            }
        }
    }
    if(out.title.empty()) {out.title=out.body.substr(0,out.body.find('\n'));if(out.title.empty())out.title="Untitled note";}
    return out;
}
inline std::string payload(const Command& c) {
    auto type=c.name=="task"?"checklist":c.name=="idea"?"project":"markdown";
    std::string life=c.name=="someday" || c.name=="soon" || c.name=="disposable"?c.name:"reference";
    std::string content=c.name=="task"?"{\"items\":[{\"text\":"+quote(c.text)+",\"done\":false}]}":
        c.name=="idea"?"{\"idea\":"+quote(c.text)+",\"stage\":\"captured\"}":"{\"markdown\":"+quote(c.text)+"}";
    return "{\"type\":"+quote(type)+",\"lifecycle\":"+quote(life)+",\"content\":"+content+"}";
}
struct Reply {bool ok=false,auth=false;std::string error;std::vector<Note> notes;};
class Client {
    std::string cookie;
    using Json=std::unique_ptr<JsonParser,decltype(&g_object_unref)>;
    Json request(const std::string& action,const std::string& body={}) {
        std::vector<std::string> args={"curl","--silent","--show-error","--proto","=https","--connect-timeout","5","--max-time","20","--max-filesize","8388608","--cookie",cookie,"--cookie-jar",cookie,"https://notes.aridlin.pl/api.php?action="+action};
        if(!body.empty()){args.insert(args.end(),{"--header","Content-Type: application/json","--data-binary","@-"});}
        std::vector<const gchar*> argv;for(auto& arg:args)argv.push_back(arg.c_str());argv.push_back(nullptr);
        GError* error=nullptr;
        GSubprocess* process=g_subprocess_newv(argv.data(),GSubprocessFlags(G_SUBPROCESS_FLAGS_STDIN_PIPE|G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE),&error);
        if(!process){g_clear_error(&error);throw std::runtime_error("Cannot start curl. Install curl to sync notes.");}
        gchar *out=nullptr,*err=nullptr;
        bool ok=g_subprocess_communicate_utf8(process,body.c_str(),nullptr,&out,&err,&error);
        bool success=ok && g_subprocess_get_successful(process);
        g_object_unref(process);g_free(err);g_clear_error(&error);
        std::string data=out?out:"";g_free(out);
        g_chmod(cookie.c_str(),0600);
        if(!success)throw std::runtime_error("Cannot reach notes.aridlin.pl. Check your connection. For a save, check *read before retrying: delivery was not confirmed.");
        Json json(json_parser_new(),g_object_unref);
        if(!json_parser_load_from_data(json.get(),data.c_str(),data.size(),nullptr) || !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(json.get())))
            throw std::runtime_error("Invalid response from notes.aridlin.pl. For a save, check *read before retrying.");
        auto root=json_node_get_object(json_parser_get_root(json.get()));
        if(!json_object_has_member(root,"ok") || !json_object_get_boolean_member(root,"ok")) {
            auto code=field(root,"error");
            if(code=="authentication_required")throw std::runtime_error("Sign in with *login.");
            if(code=="login_failed")throw std::runtime_error("Sign-in failed. Check your username and password.");
            throw std::runtime_error("Notes server: "+code);
        }
        return json;
    }
    static JsonObject* root(const Json& j){return json_node_get_object(json_parser_get_root(j.get()));}
public:
    // Accessed by one GTask at a time. Passwords only travel over stdin to curl.
    Reply run(const Command& c,const std::string& username={},const std::string& password={}) {
        Reply result;
        try {
            std::string dir=std::string(g_get_user_data_dir())+"/kalwer/notes";
            if(g_mkdir_with_parents(dir.c_str(),0700)!=0 || g_chmod(dir.c_str(),0700)!=0)
                throw std::runtime_error("Cannot create private notes session directory.");
            cookie=dir+"/cookies";
            struct stat info{};
            if(lstat(cookie.c_str(),&info)==0 && (!S_ISREG(info.st_mode) || info.st_uid!=getuid()))
                throw std::runtime_error("Unsafe notes cookie file; remove it before signing in.");
            if(g_file_test(cookie.c_str(),G_FILE_TEST_EXISTS))g_chmod(cookie.c_str(),0600);
            int fd=open(cookie.c_str(),O_WRONLY|O_CREAT|O_NOFOLLOW,0600);
            if(fd<0)throw std::runtime_error("Cannot open private notes cookie file.");
            fchmod(fd,0600);close(fd);
            auto session=request("session");auto s=root(session);
            auto csrf=field(s,"csrf");
            if(c.name=="login") {
                request("login","{\"csrf\":"+quote(csrf)+",\"username\":"+quote(username)+",\"password\":"+quote(password)+"}");
            } else {
                if(!json_object_has_member(s,"authenticated") || !json_object_get_boolean_member(s,"authenticated")) {result.auth=true;result.error="Sign in to notes.aridlin.pl";return result;}
                if(c.name=="logout")request("logout","{\"csrf\":"+quote(csrf)+"}");
                else if(c.save) {
                    auto body=payload(c);body.pop_back();body+=",\"csrf\":"+quote(csrf)+"}";
                    auto saved=request("save-note",body);
                    auto n=json_object_get_member(root(saved),"note");
                    if(!n || !JSON_NODE_HOLDS_OBJECT(n) || field(json_node_get_object(n),"id").empty())throw std::runtime_error("Save not confirmed. Check *read before retrying.");
                    result.notes.push_back(decode_note(json_node_get_object(n)));
                } else {
                    auto data=request("bootstrap");auto node=json_object_get_member(root(data),"notes");
                    if(!node || !JSON_NODE_HOLDS_ARRAY(node))throw std::runtime_error("Notes list missing from server response.");
                    auto list=json_node_get_array(node);
                    for(guint i=0;i<json_array_get_length(list);++i){auto n=json_array_get_element(list,i);if(JSON_NODE_HOLDS_OBJECT(n))result.notes.push_back(decode_note(json_node_get_object(n)));}
                }
            }
            result.ok=true;
        } catch(const std::exception& e){result.error=e.what();}
        return result;
    }
};
}
