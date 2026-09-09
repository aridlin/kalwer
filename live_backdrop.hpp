#pragma once
#include "appearance.hpp"
#include <atomic>
#include <mutex>
#include <memory>
#include <thread>
#include <chrono>
#ifndef _WIN32
#include "backdrop_linux.hpp"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#endif
namespace kalwer {
struct LiveBackdrop {
    struct Frame { int width=0,height=0; std::vector<uint32_t> pixels; };
    std::mutex mutex;
    std::shared_ptr<const Frame> frame;
    std::atomic<bool> stop{false};
    bool active=false;
    int mode=0,theme=0,scale=3;
#ifdef _WIN32
    HWND window=nullptr;
#endif
    std::thread worker;
    ~LiveBackdrop(){ stop=true;if(worker.joinable())worker.join(); }
    void start(){ if(!worker.joinable())worker=std::thread([this]{run();}); }
    void configure(bool enabled,int m,int t,int s) { std::lock_guard lock(mutex);active=enabled;mode=m;theme=t;scale=s;if(!enabled)frame={}; }
    std::shared_ptr<const Frame> copy(){std::lock_guard lock(mutex);return frame;}
    void run() {
        while(!stop) {
            bool enabled;int m,t,s;
            {std::lock_guard lock(mutex);enabled=active;m=mode;t=theme;s=scale;}
            if(enabled && m>0) {
                Frame next;
#ifdef _WIN32
                HWND target;{std::lock_guard lock(mutex);target=window;}
                RECT r{};
                if(target && GetWindowRect(target,&r)) {
                    int w=std::max(1L,(r.right-r.left)),h=std::max(1L,(r.bottom-r.top));
                    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
                    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
                    if(bitmap && bits) {auto old=SelectObject(dc,bitmap);SetStretchBltMode(dc,HALFTONE);
                        if(StretchBlt(dc,0,0,w,h,screen,r.left,r.top,r.right-r.left,r.bottom-r.top,SRCCOPY|CAPTUREBLT)) {next.width=w;next.height=h;next.pixels.assign(static_cast<uint32_t*>(bits),static_cast<uint32_t*>(bits)+size_t(w)*h);}
                        SelectObject(dc,old);DeleteObject(bitmap);
                    }
                    DeleteDC(dc);ReleaseDC(nullptr,screen);
                }
#else
                next=linux_frame(2);
#endif
                if(!next.pixels.empty()) {
                    std::lock_guard lock(mutex); if(active && mode==m && theme==t && scale==s)frame=std::make_shared<const Frame>(std::move(next));
                }
            }
            for(int i=0;i<10 && !stop;++i)std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    }
#ifndef _WIN32
    static Frame linux_frame(int pixel_scale) {
        if(!std::getenv("HYPRLAND_INSTANCE_SIGNATURE"))return {};
        GError* error=nullptr; GSubprocess* process=g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE,&error,"hyprctl","-j","clients",nullptr);
        if(!process){if(error)g_error_free(error);return {};}
        gchar* json=nullptr;g_subprocess_communicate_utf8(process,nullptr,nullptr,&json,nullptr,&error);g_object_unref(process);
        if(error){g_error_free(error);g_free(json);return {};}
        JsonParser* parser=json_parser_new();bool okay=json && json_parser_load_from_data(parser,json,-1,nullptr);g_free(json);
        if(!okay || !JSON_NODE_HOLDS_ARRAY(json_parser_get_root(parser))){g_object_unref(parser);return {};}
        struct Window {uint32_t id;int x,y,w,h,workspace,order;bool floating,own;};std::vector<Window> windows;Window target{};
        auto* array=json_node_get_array(json_parser_get_root(parser));
        for(guint i=0;i<json_array_get_length(array);++i) {
            auto* obj=json_array_get_object_element(array,i);if(!obj || !json_object_get_boolean_member(obj,"mapped") || json_object_get_boolean_member(obj,"hidden"))continue;
            auto* at=json_object_get_array_member(obj,"at");auto* size=json_object_get_array_member(obj,"size");auto* workspace=json_object_get_object_member(obj,"workspace");
            if(!at || !size || !workspace)continue;
            Window w{};const char* address=json_object_get_string_member(obj,"address");if(!address)continue;
            try{w.id=uint32_t(std::stoull(address,nullptr,16));}catch(...){continue;}
            w.x=json_array_get_int_element(at,0);w.y=json_array_get_int_element(at,1);w.w=json_array_get_int_element(size,0);w.h=json_array_get_int_element(size,1);w.workspace=json_object_get_int_member(workspace,"id");w.order=json_object_get_int_member(obj,"focusHistoryID");w.floating=json_object_get_boolean_member(obj,"floating");w.own=json_object_get_int_member(obj,"pid")==getpid();
            const char* title=json_object_get_string_member(obj,"title");
            if(w.own && title && (std::strcmp(title,"Kalwer")==0 || std::strcmp(title,"Kalwer Command Output")==0))target=w;
            windows.push_back(w);
        }
        g_object_unref(parser);if(target.w<=0 || target.h<=0)return {};
        Frame result{std::max(1,target.w*pixel_scale),std::max(1,target.h*pixel_scale),{}};result.pixels.assign(size_t(result.width)*result.height,0xff17241e);
        std::sort(windows.begin(),windows.end(),[](const Window& a,const Window& b){return a.floating!=b.floating?!a.floating:a.order>b.order;});
        bool captured=false;
        for(auto& w:windows)if(!w.own && w.workspace==target.workspace && w.x<target.x+target.w && w.x+w.w>target.x && w.y<target.y+target.h && w.y+w.h>target.y) {
            auto image=capture_window(w.id);if(image.pixels.empty())continue;captured=true;
            for(int y=0;y<result.height;++y)for(int x=0;x<result.width;++x) {
                int sx=target.x+x/pixel_scale-w.x,sy=target.y+y/pixel_scale-w.y;
                if(sx>=0 && sy>=0 && sx<w.w && sy<w.h)result.pixels[size_t(y)*result.width+x]=image.pixels[size_t(sy)*image.height/w.h*image.width+size_t(sx)*image.width/w.w];
            }
        }
        return captured?result:Frame{};
    }
#endif
};
inline LiveBackdrop live_backdrop;
}
