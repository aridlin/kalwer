#pragma once
#include "appearance.hpp"
#include "protocols/toplevel-client.h"
#include <wayland-client.h>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
// The unused v2 request refers to this type; we bind v1 and capture by Hyprland's numeric address.
extern "C" { extern const wl_interface zwlr_foreign_toplevel_handle_v1_interface={"zwlr_foreign_toplevel_handle_v1",1,0,nullptr,0,nullptr}; }
namespace kalwer {
struct CapturedWindow { int width=0,height=0; std::vector<uint32_t> pixels; };
inline CapturedWindow capture_window(uint32_t handle) {
    struct Capture {
        wl_display* display=nullptr; wl_registry* registry=nullptr; wl_shm* shm=nullptr;
        hyprland_toplevel_export_manager_v1* manager=nullptr;
        hyprland_toplevel_export_frame_v1* frame=nullptr; wl_buffer* buffer=nullptr;
        void* memory=MAP_FAILED; size_t size=0; int fd=-1; uint32_t width=0,height=0,stride=0,format=0,flags=0;
        bool done=false,okay=false;
        ~Capture() { if(frame)hyprland_toplevel_export_frame_v1_destroy(frame);if(buffer)wl_buffer_destroy(buffer);if(manager)hyprland_toplevel_export_manager_v1_destroy(manager);if(shm)wl_shm_destroy(shm);if(registry)wl_registry_destroy(registry);if(display)wl_display_disconnect(display);if(memory!=MAP_FAILED)munmap(memory,size);if(fd>=0)close(fd); }
    } c;
    c.display=wl_display_connect(nullptr); if(!c.display) return {};
    c.registry=wl_display_get_registry(c.display);
    static const wl_registry_listener registry_listener{
        +[](void* data,wl_registry* registry,uint32_t name,const char* interface,uint32_t) {
            auto& c=*static_cast<Capture*>(data);
            if(!std::strcmp(interface,"wl_shm")) c.shm=static_cast<wl_shm*>(wl_registry_bind(registry,name,&wl_shm_interface,1));
            if(!std::strcmp(interface,"hyprland_toplevel_export_manager_v1")) c.manager=static_cast<hyprland_toplevel_export_manager_v1*>(wl_registry_bind(registry,name,&hyprland_toplevel_export_manager_v1_interface,1));
        },+[](void*,wl_registry*,uint32_t){}
    };
    wl_registry_add_listener(c.registry,&registry_listener,&c); if(wl_display_roundtrip(c.display)<0 || !c.manager || !c.shm) return {};
    c.frame=hyprland_toplevel_export_manager_v1_capture_toplevel(c.manager,0,handle);
    static const hyprland_toplevel_export_frame_v1_listener listener{
        +[](void* d,hyprland_toplevel_export_frame_v1*,uint32_t f,uint32_t w,uint32_t h,uint32_t stride){auto& c=*static_cast<Capture*>(d);c.format=f;c.width=w;c.height=h;c.stride=stride;},
        +[](void*,hyprland_toplevel_export_frame_v1*,uint32_t,uint32_t,uint32_t,uint32_t){},
        +[](void* d,hyprland_toplevel_export_frame_v1*,uint32_t flags){static_cast<Capture*>(d)->flags=flags;},
        +[](void* d,hyprland_toplevel_export_frame_v1*,uint32_t,uint32_t,uint32_t){auto& c=*static_cast<Capture*>(d);c.done=c.okay=true;},
        +[](void* d,hyprland_toplevel_export_frame_v1*){static_cast<Capture*>(d)->done=true;},
        +[](void*,hyprland_toplevel_export_frame_v1*,uint32_t,uint32_t,uint32_t){},
        +[](void* d,hyprland_toplevel_export_frame_v1* frame){
            auto& c=*static_cast<Capture*>(d);
            if(!c.width || !c.height || c.width>8192 || c.height>8192 || c.stride<c.width*4 || c.stride>65536 || (c.format!=WL_SHM_FORMAT_ARGB8888 && c.format!=WL_SHM_FORMAT_XRGB8888)){c.done=true;return;}
            c.size=size_t(c.stride)*c.height; if(c.size>128*1024*1024){c.done=true;return;}
            c.fd=memfd_create("kalwer-backdrop",MFD_CLOEXEC); if(c.fd<0 || ftruncate(c.fd,off_t(c.size))<0){c.done=true;return;}
            c.memory=mmap(nullptr,c.size,PROT_READ|PROT_WRITE,MAP_SHARED,c.fd,0); if(c.memory==MAP_FAILED){c.done=true;return;}
            auto* pool=wl_shm_create_pool(c.shm,c.fd,int(c.size));
            c.buffer=wl_shm_pool_create_buffer(pool,0,c.width,c.height,c.stride,c.format);wl_shm_pool_destroy(pool);
            hyprland_toplevel_export_frame_v1_copy(frame,c.buffer,1);
        }
    };
    hyprland_toplevel_export_frame_v1_add_listener(c.frame,&listener,&c);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(400);
    while(!c.done && std::chrono::steady_clock::now()<deadline) {
        if(wl_display_dispatch_pending(c.display)<0)break;
        wl_display_flush(c.display);pollfd fd{wl_display_get_fd(c.display),POLLIN,0};
        if(poll(&fd,1,40)>0 && wl_display_dispatch(c.display)<0)break;
    }
    if(!c.okay || c.memory==MAP_FAILED)return {};
    CapturedWindow out{int(c.width),int(c.height),std::vector<uint32_t>(size_t(c.width)*c.height)};
    for(uint32_t y=0;y<c.height;++y) {
        auto* row=reinterpret_cast<uint32_t*>(static_cast<char*>(c.memory)+size_t(c.flags&1?c.height-1-y:y)*c.stride);
        for(uint32_t x=0;x<c.width;++x)out.pixels[size_t(y)*c.width+x]=row[x]|0xff000000;
    }
    return out;
}
}
