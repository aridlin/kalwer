#pragma once
#include "appearance.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <thread>
#include <mutex>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <spawn.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
extern char** environ;
#endif
namespace kalwer::koom {
inline std::filesystem::path utf8_path(std::string_view text){return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));}
inline std::string filename(const std::filesystem::path& path){auto text=path.filename().u8string();return std::string(text.begin(),text.end());}
inline bool write_bundle_file(const std::filesystem::path& path,const void* data,size_t size) {std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(static_cast<const char*>(data),size);return bool(out);}

inline std::function<bool(const std::filesystem::path&)> install_bundle;
inline std::filesystem::path directory(){return wallet.path.parent_path()/"koom";}
inline bool valid_wad(const std::filesystem::path& path,bool* iwad=nullptr) {
    std::ifstream in(path,std::ios::binary);unsigned char h[12]{};if(!in.read(reinterpret_cast<char*>(h),12))return false;
    bool base=std::memcmp(h,"IWAD",4)==0;if(!base && std::memcmp(h,"PWAD",4)!=0)return false;
    auto u32=[](const unsigned char* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;};
    std::error_code ec;auto size=std::filesystem::file_size(path,ec);uint64_t count=u32(h+4),offset=u32(h+8);
    if(ec || !count || count>1000000 || offset<12 || offset+count*16>size)return false;
    in.seekg(offset);
    for(uint64_t i=0;i<count;i++){unsigned char lump[16]{};if(!in.read(reinterpret_cast<char*>(lump),16) || uint64_t(u32(lump))+u32(lump+4)>size)return false;}
    if(iwad)*iwad=base;
    return true;
}
inline std::string import_wad(std::filesystem::path source) {
    if(!valid_wad(source))return "Not a valid IWAD/PWAD file.";
    auto target=directory()/"wads"/source.filename();std::error_code ec;std::filesystem::create_directories(target.parent_path(),ec);
    if(ec)return "Could not create the WAD directory.";
    if(std::filesystem::exists(target))return "A WAD with that name is already installed. Rename the source to import another copy.";
    std::filesystem::copy_file(source,target,std::filesystem::copy_options::none,ec);
    return ec?"Could not copy the WAD.":"Installed "+filename(source)+". Open /koom and select it with Left/Right.";
}
struct Session {
    std::mutex mutex;std::condition_variable wake;
    bool stop=false,focused=false;std::array<unsigned char,256> keys{};
    std::vector<uint32_t> pixels;uint64_t sequence=0;uint32_t completed_maps=0;std::string error="Loading Koom…";
#ifdef _WIN32
    HANDLE process=nullptr;
#else
    pid_t process=0;
#endif
    std::thread thread;
    explicit Session(std::filesystem::path wad,std::filesystem::path base={}):thread([this,wad,base]{run(wad,base);}){}
    ~Session(){
        {std::lock_guard lock(mutex);stop=true;
#ifdef _WIN32
        if(process)TerminateProcess(process,0);
#else
        if(process>0)kill(process,SIGTERM);
#endif
        }wake.notify_all();if(thread.joinable())thread.join();
    }
    void input(bool focus,const std::array<unsigned char,256>& pressed){std::lock_guard lock(mutex);focused=focus;keys=pressed;if(!focus)keys.fill(0);wake.notify_one();}
    void fail(std::string text){std::lock_guard lock(mutex);error=std::move(text);}
    void run(std::filesystem::path wad,std::filesystem::path selected_base) {
#ifndef _WIN32
        // A dead helper must never deliver SIGPIPE to the launcher.
        sigset_t blocked;sigemptyset(&blocked);sigaddset(&blocked,SIGPIPE);pthread_sigmask(SIG_BLOCK,&blocked,nullptr);
#endif
        const auto dir=directory();
        try{if(!install_bundle || !install_bundle(dir)){fail("Could not install the bundled Koom runtime.");return;}}catch(...){fail("Could not prepare Koom files.");return;}
        auto base=selected_base.empty()?dir/"freedoom2.wad":selected_base;bool is_base=false;
        if(!valid_wad(base,&is_base) || !is_base){fail("The selected base IWAD is invalid.");return;}
        is_base=false;
        if(!wad.empty() && !valid_wad(wad,&is_base)){fail("This WAD is invalid or unreadable.");return;}
        if(is_base)base=wad;
        auto config=dir/"doom.cfg";
        FILE* input=nullptr;FILE* output=nullptr;
#ifdef _WIN32
        auto executable=dir/"kalwer-koom.exe";
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};HANDLE child_in=nullptr,write_in=nullptr,read_out=nullptr,child_out=nullptr;
        if(!CreatePipe(&child_in,&write_in,&attributes,0) || !CreatePipe(&read_out,&child_out,&attributes,0)){for(HANDLE h:{child_in,write_in,read_out,child_out})if(h)CloseHandle(h);fail("Could not open Koom pipes.");return;}
        SetHandleInformation(write_in,HANDLE_FLAG_INHERIT,0);SetHandleInformation(read_out,HANDLE_FLAG_INHERIT,0);
        HANDLE log=CreateFileW((dir/"runtime.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&attributes,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        auto quote=[](const std::filesystem::path& p){return L"\""+p.wstring()+L"\"";};
        std::wstring command=quote(executable)+L" -iwad "+quote(base)+L" -config "+quote(config)+L" -soundfont "+quote(dir/"TimGM6mb.sf2");
        if(!wad.empty() && !is_base)command+=L" -file "+quote(wad);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdInput=child_in;startup.hStdOutput=child_out;startup.hStdError=log;
        PROCESS_INFORMATION child{};bool created=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,dir.c_str(),&startup,&child);
        CloseHandle(child_in);CloseHandle(child_out);if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);
        if(!created){CloseHandle(write_in);CloseHandle(read_out);fail("Could not start Koom.");return;}
        CloseHandle(child.hThread);{std::lock_guard lock(mutex);process=child.hProcess;if(stop)TerminateProcess(process,0);}
        input=_fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(write_in),_O_BINARY),"wb");output=_fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(read_out),_O_BINARY),"rb");
#else
        auto executable=dir/"kalwer-koom";int to_child[2],from_child[2];
        if(pipe(to_child)){fail("Could not open Koom pipes.");return;}
        if(pipe(from_child)){close(to_child[0]);close(to_child[1]);fail("Could not open Koom pipes.");return;}
        posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,to_child[0],0);posix_spawn_file_actions_adddup2(&actions,from_child[1],1);
        posix_spawn_file_actions_addopen(&actions,2,(dir/"runtime.log").c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
        for(int fd:{to_child[0],to_child[1],from_child[0],from_child[1]})posix_spawn_file_actions_addclose(&actions,fd);
        std::vector<std::string> args{executable.string(),"-iwad",base.string(),"-config",config.string(),"-soundfont",(dir/"TimGM6mb.sf2").string()};
        if(!wad.empty() && !is_base){args.push_back("-file");args.push_back(wad.string());}
        std::vector<char*> argv;for(auto& arg:args)argv.push_back(arg.data());argv.push_back(nullptr);
        pid_t pid=0;int result=posix_spawn(&pid,executable.c_str(),&actions,nullptr,argv.data(),environ);posix_spawn_file_actions_destroy(&actions);close(to_child[0]);close(from_child[1]);
        if(result){close(to_child[1]);close(from_child[0]);fail("Could not start Koom.");return;}
        {std::lock_guard lock(mutex);process=pid;if(stop)kill(pid,SIGTERM);}
        input=fdopen(to_child[1],"wb");output=fdopen(from_child[0],"rb");
#endif
        if(input)setvbuf(input,nullptr,_IONBF,0);
        auto deadline=std::chrono::steady_clock::now();
        if(input && output)for(;;){
            std::array<unsigned char,256> pressed;
            {std::unique_lock lock(mutex);if(!focused){wake.wait(lock,[&]{return stop || focused;});deadline=std::chrono::steady_clock::now();}if(stop)break;pressed=keys;}
            // Rendering and pipe transfer belong inside the audio period.
            // Sleeping a full period afterwards starves the playback device.
            deadline+=std::chrono::milliseconds(29);
            if(fwrite(pressed.data(),1,pressed.size(),input)!=pressed.size())break;
            uint32_t header[4]{};if(fread(header,4,4,output)!=4 || header[0]!=0x4b4f4f4d || header[1]!=320 || header[2]!=200)break;
            std::vector<uint32_t> frame(320*200);if(fread(frame.data(),4,frame.size(),output)!=frame.size())break;
            {std::lock_guard lock(mutex);pixels=std::move(frame);++sequence;completed_maps=header[3];error.clear();}
            auto now=std::chrono::steady_clock::now();
            if(now-deadline>std::chrono::milliseconds(100))deadline=now;
            std::unique_lock lock(mutex);wake.wait_until(lock,deadline,[&]{return stop || !focused;});if(stop)break;
        }
        if(input)fclose(input);
        if(output)fclose(output);
#ifdef _WIN32
        {std::lock_guard lock(mutex);if(process){TerminateProcess(process,0);CloseHandle(process);process=nullptr;}}
#else
        {std::lock_guard lock(mutex);if(process>0){kill(process,SIGTERM);waitpid(process,nullptr,0);process=0;}}
#endif
        fail("Koom stopped. Press R to restart or choose another WAD.");
    }
};
struct Game {
    std::shared_ptr<Session> session;std::vector<std::filesystem::path> wads,bases;int selected=0,base_selected=0;uint32_t rewarded_maps=0;
    std::array<unsigned char,256> keys{};
    void reset(){session.reset();rewarded_maps=0;keys.fill(0);wads.clear();wads.push_back({});bases.clear();bases.push_back({});std::error_code ec;
        for(std::filesystem::directory_iterator it(directory()/"wads",ec),end;!ec && it!=end;it.increment(ec))if(it->is_regular_file() && valid_wad(it->path()))wads.push_back(it->path());
        std::sort(wads.begin()+1,wads.end());
        for(const auto& path:wads)if(!path.empty()){bool base=false;if(valid_wad(path,&base) && base)bases.push_back(path);}
        selected=std::clamp(selected,0,int(wads.size())-1);base_selected=std::clamp(base_selected,0,int(bases.size())-1);
    }
    void key(int k,bool down=true){
        if(!session){if(wads.empty())reset();if(down && k==9)base_selected=(base_selected+1)%bases.size();if(down && k==1)selected=(selected+int(wads.size())-1)%wads.size();if(down && k==2)selected=(selected+1)%wads.size();if(down && (k==13 || k==' '))session=std::make_shared<Session>(wads[selected],bases[base_selected]);return;}
        int code=k==1?0xac:k==2?0xae:k==3 || k=='w'?0xad:k==4 || k=='s'?0xaf:k=='a'?0xa0:k=='d'?0xa1:k=='e'?0xa2:k==' ' || k==17?0xa3:k=='q'?27:k;
        if(code>=0 && code<256)keys[code]=down;
    }
    void focus(bool active){
        if(!active)keys.fill(0);
        if(session){
            session->input(active,keys);
            uint32_t completed;{std::lock_guard lock(session->mutex);completed=session->completed_maps;}
            if(active && rewarded_maps<completed && wallet.credit(100,true))++rewarded_maps;
        }
    }
    template<class P>void draw(P& p)const {
        if(!session){p.text(28,100,23,"KOOM",0x8ce9b3);p.text(28,150,14,wads.empty() || wads[selected].empty()?"Freedoom: Phase 2 (included)":filename(wads[selected]),0xe0f5e8);p.text(28,195,12,"Left / Right: choose WAD   Enter: play",0x8dada1);p.text(28,228,11,"Tab: PWAD base - "+(bases.empty() || bases[base_selected].empty()?std::string("Freedoom"):filename(bases[base_selected])),0x8dada1);p.text(28,265,12,"/wad-import <path> installs your own WAD",0x8dada1);return;}
        std::lock_guard lock(session->mutex);
        if(!session->pixels.empty()){
            if constexpr(requires{p.image(10,105,400,300,session->pixels.data(),320,200);})p.image(10,105,400,300,session->pixels.data(),320,200);
        }
        if(!session->error.empty()) {p.rect(18,190,384,58,0x081a18);p.text(25,215,11,session->error,0xffd579);}
        p.text(18,65,11,"Arrows: move/turn   WASD: strafe   Space: fire",0x8dada1);
        p.text(18,85,11,"E: use   Q: Doom menu   1-7: weapon",0x8dada1);
        p.text(18,432,11,"Map cleared: +100 koins   R: WAD selection",0x8dada1);
    }
};
}
