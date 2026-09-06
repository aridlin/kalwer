#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "vendor/everything/everything_ipc.h"
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace kalwer_files {
namespace fs = std::filesystem;
inline std::string text(const fs::path& p) { auto s=p.u8string(); return {reinterpret_cast<const char*>(s.data()),s.size()}; }
inline fs::path from_utf8(const std::string& s) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()),s.size())); }
inline std::string fold(std::string s) { for(auto& c:s) if(c>='A'&&c<='Z')c+='a'-'A'; return s; }
struct Entry { std::string path,name; };
struct Reply { unsigned long long request=0,revision=0; std::vector<Entry> entries; std::string status; };
inline std::vector<std::string> search_terms(const std::string& query) {
    std::vector<std::string> out; std::string term; bool quoted=false;
    for(char c:query) {
        if(c=='"') { quoted=!quoted; continue; }
        if(!quoted && (c==' '||c=='\t')) { if(!term.empty()) {out.push_back(term);term.clear();} }
        else term+=c;
    }
    if(!term.empty())out.push_back(term);
    return out;
}
inline void rank(std::vector<Entry>& entries,const std::string& query) {
    const auto q=fold(query);
    auto score=[&](const Entry& e) { const auto n=fold(e.name); return n==q?0:n.starts_with(q)?1:n.find(q)!=std::string::npos?2:3; };
    std::sort(entries.begin(),entries.end(),[&](const auto& a,const auto& b) {
        auto x=score(a),y=score(b); return x!=y?x<y:a.path<b.path;
    });
    entries.erase(std::unique(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.path==b.path;}),entries.end());
    if(entries.size()>512)entries.resize(512);
}
#ifndef _WIN32
struct ProcessResult { int code=-1; bool cancelled=false; std::string out,error; };
// argv is passed directly, never through a shell. Both pipes are drained and bounded.
inline ProcessResult run_process(const std::vector<std::string>& args,const std::function<bool()>& cancel,
                                 std::chrono::milliseconds timeout) {
    ProcessResult result; int output[2],errors[2];
    if(pipe2(output,O_CLOEXEC))return result;
    if(pipe2(errors,O_CLOEXEC)) {close(output[0]);close(output[1]);return result;}
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,output[1],STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions,errors[1],STDERR_FILENO);
    posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
    posix_spawn_file_actions_addclose(&actions,output[0]);posix_spawn_file_actions_addclose(&actions,errors[0]);
    std::vector<char*> argv;for(const auto& a:args)argv.push_back(const_cast<char*>(a.c_str()));argv.push_back(nullptr);
    pid_t pid=0;const int spawn=posix_spawnp(&pid,argv[0],&actions,nullptr,argv.data(),environ);
    posix_spawn_file_actions_destroy(&actions);close(output[1]);close(errors[1]);
    if(spawn) {close(output[0]);close(errors[0]);result.error="Unable to start "+args[0];return result;}
    fcntl(output[0],F_SETFL,O_NONBLOCK);fcntl(errors[0],F_SETFL,O_NONBLOCK);
    const auto deadline=std::chrono::steady_clock::now()+timeout;int status=0;
    auto drain=[](int fd,std::string& target,size_t limit) {char buf[8192];ssize_t n;while((n=read(fd,buf,sizeof(buf)))>0)if(target.size()<limit)target.append(buf,std::min<size_t>(n,limit-target.size()));};
    for(;;) {
        drain(output[0],result.out,16*1024*1024);drain(errors[0],result.error,16384);
        if(waitpid(pid,&status,WNOHANG)==pid)break;
        if(cancel() || std::chrono::steady_clock::now()>=deadline) {
            result.cancelled=true;kill(pid,SIGKILL);waitpid(pid,&status,0);break;
        }
        pollfd fds[]={{output[0],POLLIN,0},{errors[0],POLLIN,0}};poll(fds,2,20);
    }
    drain(output[0],result.out,16*1024*1024);drain(errors[0],result.error,16384);
    close(output[0]);close(errors[0]);result.code=WIFEXITED(status)?WEXITSTATUS(status):-1;return result;
}
#else
inline HWND everything_window() {
    if(auto w=FindWindowW(EVERYTHING_IPC_WNDCLASSW,nullptr))return w;
    return FindWindowW(L"EVERYTHING_TASKBAR_NOTIFICATION_(1.5a)",nullptr);
}
inline fs::path everything_executable() {
    for(const auto* key:{L"ProgramW6432",L"ProgramFiles",L"ProgramFiles(x86)"}) {
        if(const auto* base=_wgetenv(key)) {auto p=fs::path(base)/L"Everything"/L"Everything.exe";std::error_code e;if(fs::exists(p,e))return p;}
    }
    return {};
}
inline bool start_everything() {
    const auto path=everything_executable();if(path.empty())return false;
    std::wstring args=L"\""+path.wstring()+L"\" -startup";
    STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
    if(!CreateProcessW(path.c_str(),args.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&start,&process))return false;
    CloseHandle(process.hProcess);CloseHandle(process.hThread);return true;
}
struct EverythingResponse { DWORD id;bool received=false;std::vector<unsigned char> bytes; };
inline LRESULT CALLBACK everything_reply(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_NCCREATE)SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* reply=reinterpret_cast<EverythingResponse*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(msg==WM_COPYDATA && reply) {
        const auto* data=reinterpret_cast<COPYDATASTRUCT*>(lp);
        if(data && data->dwData==reply->id && data->lpData && data->cbData<=16*1024*1024) {
            const auto* begin=static_cast<const unsigned char*>(data->lpData);
            reply->bytes.assign(begin,begin+data->cbData);reply->received=true;return TRUE;
        }
    }
    return DefWindowProcW(w,msg,wp,lp);
}
// Official SDK IPC v1 Unicode wire format; bounds-check every returned offset.
inline std::vector<Entry> decode_everything(const std::vector<unsigned char>& data) {
    std::vector<Entry> out;constexpr size_t base=offsetof(EVERYTHING_IPC_LISTW,items);
    if(data.size()<base)return out;
    const auto* list=reinterpret_cast<const EVERYTHING_IPC_LISTW*>(data.data());
    if(list->numitems>1024 || list->numitems>(data.size()-base)/sizeof(EVERYTHING_IPC_ITEMW))return out;
    auto string_at=[&](DWORD offset) -> std::wstring {
        if(offset%sizeof(wchar_t)||offset>=data.size())return {};
        const auto* ptr=reinterpret_cast<const wchar_t*>(data.data()+offset);
        size_t capacity=(data.size()-offset)/sizeof(wchar_t),length=0;
        while(length<capacity && ptr[length])++length;
        return length<capacity?std::wstring(ptr,length):std::wstring{};
    };
    for(DWORD i=0;i<list->numitems;++i) {
        const auto name=string_at(list->items[i].filename_offset),path=string_at(list->items[i].path_offset);
        if(name.empty())continue;
        const fs::path full=path.empty()?fs::path(name):fs::path(path)/name;
        out.push_back({text(full),text(fs::path(name))});
    }
    return out;
}
#endif

class Index {
    std::mutex mutex_;std::condition_variable cv_;
    std::atomic<bool> stop_{false},refresh_{false},scanning_{false};
    std::atomic<unsigned long long> request_{0};
    std::thread search_,scan_;bool started_=false;fs::path directory_;bool maintain_;
    std::string query_,scan_error_;Reply reply_;unsigned long long generation_=0;
    void start() {
        if(started_)return;
        started_=true;
#ifndef _WIN32
        if(directory_.empty()) {const char* cache=getenv("XDG_CACHE_HOME"),*home=getenv("HOME");directory_=(cache?fs::path(cache):fs::path(home?home:"/tmp")/".cache")/"kalwer";}
        std::error_code error;fs::create_directories(directory_,error);
        fs::permissions(directory_,fs::perms::owner_all,fs::perm_options::replace,error);
        if(maintain_) {scanning_=true;scan_=std::thread([this]{scan_loop();});}
#endif
        search_=std::thread([this]{search_loop();});
    }
#ifndef _WIN32
    void scan_loop() {
        while(!stop_) {
            scanning_=true;
            const auto result=run_process({"nice","-n","15","updatedb","--database-root=/","--output="+text(directory_/"system.plocate"),
                "--prune-bind-mounts=no","--prunepaths=/proc /sys /dev /run","--prunenames=.snapshots",
                "--add-single-prunepath="+text(directory_),"--require-visibility=0"},[&]{return stop_.load();},std::chrono::minutes(45));
            std::error_code error;fs::permissions(directory_/"system.plocate",fs::perms::owner_read|fs::perms::owner_write,fs::perm_options::replace,error);
            std::unique_lock lock(mutex_);scanning_=false;
            scan_error_=result.code==0?"":("Index refresh failed: "+result.error.substr(0,180));++generation_;cv_.notify_all();
            cv_.wait_for(lock,std::chrono::minutes(15),[&]{return stop_||refresh_;});refresh_=false;
        }
    }
    Reply query_backend(const std::string& query,unsigned long long id,bool& retry) {
        (void)retry;Reply reply;reply.request=id;
        const auto terms=search_terms(query);
        if(terms.empty()) {reply.status="plocate · system-wide · type a filename or path";return reply;}
        bool usable=false;std::string failure;
        std::error_code error;const bool own=fs::exists(directory_/"system.plocate",error);
        for(bool basename:{true,false}) {
            if(basename && query.find('/')!=std::string::npos) continue;
            for(bool private_db:{true,false}) {
                if(reply.entries.size()>=512)break;
                if(private_db&&!own)continue;
                if(!private_db&&!maintain_)continue;
                std::vector<std::string> args={"plocate","-i","-0","-l","1024"};
                if(basename)args.push_back("-b");
                if(private_db) {args.push_back("-d");args.push_back(text(directory_/"system.plocate"));}
                args.push_back("--");args.insert(args.end(),terms.begin(),terms.end());
                auto result=run_process(args,[&]{return stop_||request_!=id;},std::chrono::milliseconds(1000));
                if(request_!=id||stop_)return reply;
                usable|=(result.code==0||result.code==1)&&result.error.empty();
                if(!result.error.empty())failure=result.error.substr(0,160);
                if(result.cancelled)failure="Query timed out; narrow the filename or path";
                size_t start=0,end;
                while((end=result.out.find('\0',start))!=std::string::npos) {
                    auto path=result.out.substr(start,end-start);start=end+1;
                    if(!path.empty()&&path.front()=='/')reply.entries.push_back({path,text(from_utf8(path).filename())});
                }
                rank(reply.entries,query);
            }
        }
        reply.status=usable?"plocate · system-wide":"plocate unavailable: use /index-setup";
        if(!failure.empty())reply.status+=" · "+failure;
        return reply;
    }
#else
    Reply query_backend(const std::string& query,unsigned long long id,bool& retry) {
        Reply reply;reply.request=id;HWND engine=everything_window();
        if(!engine) {
            static auto last=std::chrono::steady_clock::time_point{};
            if(std::chrono::steady_clock::now()-last>std::chrono::seconds(5)) {last=std::chrono::steady_clock::now();start_everything();}
            reply.status=everything_executable().empty()?"Everything is not installed · run /index-setup":"Starting Everything…";retry=!everything_executable().empty();return reply;
        }
        DWORD_PTR loaded=0;
        if(!SendMessageTimeoutW(engine,EVERYTHING_WM_IPC,EVERYTHING_IPC_IS_DB_LOADED,0,SMTO_ABORTIFHUNG,250,&loaded)||!loaded) {
            reply.status="Everything is building its system-wide index…";retry=true;return reply;
        }
        if(refresh_.exchange(false)) {DWORD_PTR unused;SendMessageTimeoutW(engine,EVERYTHING_WM_IPC,EVERYTHING_IPC_REBUILD_DB,0,SMTO_ABORTIFHUNG,250,&unused);reply.status="Everything is rebuilding…";retry=true;return reply;}
        if(query.empty()) {reply.status="Everything · system-wide · type a filename or path";return reply;}
        for(unsigned pass=0;pass<2 && reply.entries.size()<512;++pass) {
            if(pass==0 && (query.find('/')!=std::string::npos || query.find('\\')!=std::string::npos))continue;
        WNDCLASSW wc{};wc.lpfnWndProc=everything_reply;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"KalwerEverythingReply";RegisterClassW(&wc);
        EverythingResponse response{static_cast<DWORD>(id*2+pass),false,{}};
        HWND window=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,&response);
        if(!window) {reply.status="Could not create Everything reply endpoint";return reply;}
        const auto search=from_utf8(query).wstring();
        std::vector<unsigned char> bytes(offsetof(EVERYTHING_IPC_QUERYW,search_string)+(search.size()+1)*sizeof(wchar_t));
        auto* request=reinterpret_cast<EVERYTHING_IPC_QUERYW*>(bytes.data());
        request->reply_hwnd=static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(window));request->reply_copydata_message=response.id;
        request->search_flags=pass==0?0:EVERYTHING_IPC_MATCHPATH;request->max_results=1024;
        std::memcpy(request->search_string,search.c_str(),(search.size()+1)*sizeof(wchar_t));
        COPYDATASTRUCT data{EVERYTHING_IPC_COPYDATAQUERYW,static_cast<DWORD>(bytes.size()),bytes.data()};DWORD_PTR accepted=0;
        const bool sent=SendMessageTimeoutW(engine,WM_COPYDATA,reinterpret_cast<WPARAM>(window),reinterpret_cast<LPARAM>(&data),SMTO_ABORTIFHUNG,250,&accepted)&&accepted;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while(sent&&!response.received&&!stop_&&request_==id&&std::chrono::steady_clock::now()<deadline) {
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
            if(!response.received)MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        DestroyWindow(window);
        if(response.received) {
            auto found=decode_everything(response.bytes);
            reply.entries.insert(reply.entries.end(),found.begin(),found.end());rank(reply.entries,query);
        } else {reply.status="Everything did not answer in time; try a more specific query";break;}
        }
        if(reply.status.empty())reply.status="Everything · system-wide";
        return reply;
    }
#endif
    void search_loop() {
        unsigned long long previous=0,generation=~0ULL;bool retry=false;
        while(!stop_) {
            std::unique_lock lock(mutex_);
            if(retry)cv_.wait_for(lock,std::chrono::seconds(1),[&]{return stop_||previous!=request_||generation!=generation_;});
            else cv_.wait(lock,[&]{return stop_||previous!=request_||generation!=generation_;});
            if(stop_)return;
            previous=request_;generation=generation_;const auto query=query_;lock.unlock();
            Reply result;retry=false;
            try {result=query_backend(query,previous,retry);}catch(const std::exception& e) {result.request=previous;result.status=std::string("File search: ")+e.what();}
            lock.lock();
            if(previous==request_) {
                if(scanning_)result.status+=" · refreshing index";
                if(!scan_error_.empty())result.status+=" · "+scan_error_;
                if(result.entries.empty()&&!query.empty())result.status+=" · no matches yet";
                result.revision=reply_.revision+1;reply_=std::move(result);
            }
        }
    }
public:
    explicit Index(fs::path directory={},bool maintain=true):directory_(std::move(directory)),maintain_(maintain){}
    ~Index() {stop_=true;cv_.notify_all();if(search_.joinable())search_.join();if(scan_.joinable())scan_.join();}
    unsigned long long request(std::string query) {std::lock_guard lock(mutex_);query_=std::move(query);++request_;start();cv_.notify_all();return request_;}
    void refresh() {std::lock_guard lock(mutex_);start();refresh_=true;++generation_;cv_.notify_all();}
    Reply poll(unsigned long long after=0) {std::lock_guard lock(mutex_);return reply_.revision==after?Reply{}:reply_;}
    bool busy() const { return scanning_.load(); }
    std::string status() {std::lock_guard lock(mutex_);return reply_.status.empty()?"Use :query to start system-wide file search.":reply_.status;}
};
}
