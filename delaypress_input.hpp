#pragma once
#include <gtk/gtk.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <charconv>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#ifdef KALWER_HAVE_XTEST
#include <gdk/gdkx.h>
#include <X11/extensions/XTest.h>
#include <X11/XKBlib.h>
#endif

namespace kalwer::delaypress {
struct Key { const char* name; unsigned short linux_code; guint keysym; };
inline constexpr Key keys[] = {
    {"Enter", KEY_ENTER, GDK_KEY_Return}, {"Tab", KEY_TAB, GDK_KEY_Tab},
    {"Space", KEY_SPACE, GDK_KEY_space}, {"Backspace", KEY_BACKSPACE, GDK_KEY_BackSpace},
    {"Left", KEY_LEFT, GDK_KEY_Left}, {"Right", KEY_RIGHT, GDK_KEY_Right},
    {"Up", KEY_UP, GDK_KEY_Up}, {"Down", KEY_DOWN, GDK_KEY_Down},
    {"Home", KEY_HOME, GDK_KEY_Home}, {"End", KEY_END, GDK_KEY_End},
};
struct Stroke {
    guint keycode;
    bool shift, altgr;
};
inline std::optional<std::vector<Stroke>> text_strokes(const char* text, int group) {
    if (!g_utf8_validate(text,-1,nullptr)) return {};
    auto* map=gdk_keymap_get_for_display(gdk_display_get_default());
#ifdef KALWER_HAVE_XTEST
    auto* display=gdk_display_get_default();
    XkbStateRec state{};
    if (GDK_IS_X11_DISPLAY(display) && XkbGetState(GDK_DISPLAY_XDISPLAY(display),XkbUseCoreKbd,&state)==0)
        group=state.group;
#endif
    const auto locks=gdk_keymap_get_caps_lock_state(map)?GDK_LOCK_MASK:0;
    std::vector<Stroke> result;
    std::unordered_map<guint,Stroke> cache;
    for (const char* pos=text; *pos; pos=g_utf8_next_char(pos)) {
        auto c=g_utf8_get_char(pos);
        if (c=='\r' && pos[1]=='\n') continue;
        if (g_unichar_iscntrl(c) && c!='\r' && c!='\n' && c!='\t') return {};
        guint symbol=c=='\r' || c=='\n'?GDK_KEY_Return:c=='\t'?GDK_KEY_Tab:gdk_unicode_to_keyval(c);
        if (auto found=cache.find(symbol); found!=cache.end()) {
            result.push_back(found->second);
            continue;
        }
        GdkKeymapKey* candidates=nullptr;
        int count=0;
        gdk_keymap_get_entries_for_keyval(map,symbol,&candidates,&count);
        std::optional<Stroke> selected;
        for (int mods : {0,int(GDK_SHIFT_MASK),int(GDK_MOD5_MASK),int(GDK_SHIFT_MASK|GDK_MOD5_MASK)}) {
            for (int i=0;i<count;++i) {
                const guint code=candidates[i].keycode;
                guint translated=0;
                if (code>=9 && code<=255 &&
                    gdk_keymap_translate_keyboard_state(map,code,GdkModifierType(locks|mods),group,
                        &translated,nullptr,nullptr,nullptr) && translated==symbol) {
                    selected=Stroke{code,bool(mods&GDK_SHIFT_MASK),bool(mods&GDK_MOD5_MASK)};
                    break;
                }
            }
            if (selected) break;
        }
        g_free(candidates);
        if (!selected) return {};
        cache.emplace(symbol,*selected);
        result.push_back(*selected);
    }
    return result;
}
struct Request { Key key; int seconds; std::string label{}; std::optional<Stroke> stroke{}; };
inline std::optional<Request> parse(const std::string& command) {
    std::istringstream in(command);
    std::string verb, key, delay, extra;
    if (!(in >> verb >> key >> delay) || in >> extra || g_ascii_strcasecmp(verb.c_str(), "/delaypress"))
        return {};
    auto duration=[](std::string_view value) {
        if (value.ends_with('s') || value.ends_with('S')) value.remove_suffix(1);
        int seconds=0;
        auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),seconds);
        return error==std::errc{} && end==value.data()+value.size() && seconds>=1 && seconds<=86400
            ? seconds : 0;
    };
    int seconds=duration(delay);
    const int first_seconds=duration(key);
    if (first_seconds && (!seconds || key.ends_with('s') || key.ends_with('S'))) {
        std::swap(key,delay);
        seconds=first_seconds;
    }
    if (!seconds) return {};
    for (auto k : keys)
        if (!g_ascii_strcasecmp(k.name,key.c_str()) || (k.linux_code==KEY_ENTER && !g_ascii_strcasecmp(key.c_str(),"return")))
            return Request{k,seconds};
    auto* display=gdk_display_get_default();
    if (!display) return {};
    auto* map=gdk_keymap_get_for_display(display);
    if (g_utf8_validate(key.c_str(),-1,nullptr) && g_utf8_strlen(key.c_str(),-1)==1) {
        if (auto strokes=text_strokes(key.c_str(),0); strokes && strokes->size()==1)
            return Request{keys[0],seconds,key,strokes->front()};
        return {};
    }
    std::string name=key;
    if (!g_ascii_strcasecmp(key.c_str(),"esc")) name="Escape";
    else if (!g_ascii_strcasecmp(key.c_str(),"ctrl")) name="Control_L";
    else if (!g_ascii_strcasecmp(key.c_str(),"shift")) name="Shift_L";
    else if (!g_ascii_strcasecmp(key.c_str(),"alt")) name="Alt_L";
    else if (!g_ascii_strcasecmp(key.c_str(),"super")) name="Super_L";
    guint symbol=gdk_keyval_from_name(name.c_str());
    if (symbol==GDK_KEY_VoidSymbol && !name.empty()) {
        name[0]=g_ascii_toupper(name[0]);
        symbol=gdk_keyval_from_name(name.c_str());
    }
    if (symbol==GDK_KEY_VoidSymbol) return {};
    GdkKeymapKey* candidates=nullptr;
    int count=0;
    gdk_keymap_get_entries_for_keyval(map,symbol,&candidates,&count);
    std::optional<Request> result;
    for (int i=0;i<count;++i) {
        auto candidate=candidates[i];
        if (candidate.level==0 && candidate.keycode>=9 && candidate.keycode<=255) {
            result=Request{{nullptr,static_cast<unsigned short>(candidate.keycode-8),symbol},seconds,name};
            break;
        }
    }
    g_free(candidates);
    return result;
}

// The shortcut exists only while a timer is armed. No keyboard stream is opened.
class Input {
    static constexpr auto service = "org.kde.kglobalaccel";
    static constexpr auto component = "kalwer-delaypress";
    static constexpr auto action = "_k_session:cancel";
    GDBusConnection* bus = nullptr;
    guint subscription = 0;
    bool registered = false;
    int device = -1;
    Key key = keys[0];
    void (*canceled)() = nullptr;
#ifdef KALWER_HAVE_XTEST
    GdkDisplay* display = nullptr;
    KeyCode escape = 0, target = 0;
    static GdkFilterReturn filter(GdkXEvent* raw,GdkEvent*,gpointer data) {
        auto& self = *static_cast<Input*>(data);
        auto* e = static_cast<XEvent*>(raw);
        if (e->type != KeyPress || e->xkey.keycode != self.escape) return GDK_FILTER_CONTINUE;
        self.canceled();
        return GDK_FILTER_REMOVE;
    }
    bool grab_x11() {
        display = gdk_display_get_default();
        if (!GDK_IS_X11_DISPLAY(display)) { display=nullptr; return false; }
        auto* x = GDK_DISPLAY_XDISPLAY(display);
        int event, error, major, minor;
        target = XKeysymToKeycode(x,key.keysym);
        if (!target || !XTestQueryExtension(x,&event,&error,&major,&minor)) return false;
        escape = XKeysymToKeycode(x,GDK_KEY_Escape);
        gdk_x11_display_error_trap_push(display);
        XGrabKey(x,escape,AnyModifier,DefaultRootWindow(x),False,GrabModeAsync,GrabModeAsync);
        if (gdk_x11_display_error_trap_pop(display)) { escape=0; return false; }
        gdk_window_add_filter(nullptr,filter,this);
        return true;
    }
#endif
    GVariant* call(const char* method,GVariant* args) {
        return g_dbus_connection_call_sync(bus,service,"/kglobalaccel","org.kde.KGlobalAccel",
            method,args,nullptr,G_DBUS_CALL_FLAGS_NO_AUTO_START,1000,nullptr,nullptr);
    }
    bool grab_kde() {
        constexpr gint32 esc = 0x01000000; // Qt::Key_Escape
        const gchar* id[] = {component,action,"Kalwer","Cancel delayed keypress",nullptr};
        bus = g_bus_get_sync(G_BUS_TYPE_SESSION,nullptr,nullptr);
        if (!bus) return false;
        auto* reply = call("doRegister",g_variant_new("(^as)",id));
        if (!reply) return false;
        g_variant_unref(reply);
        registered = true;
        reply = call("getComponent",g_variant_new("(s)",component));
        if (!reply) return false;
        const char* path;
        g_variant_get(reply,"(&o)",&path);
        subscription = g_dbus_connection_signal_subscribe(bus,service,"org.kde.kglobalaccel.Component",
            "globalShortcutPressed",path,component,G_DBUS_SIGNAL_FLAGS_NONE,
            +[](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* args,gpointer data){
                auto& self = *static_cast<Input*>(data);
                const char* name;
                g_variant_get_child(args,1,"&s",&name);
                if (self.registered && !g_strcmp0(name,action)) self.canceled();
            },this,nullptr);
        g_variant_unref(reply);
        auto* sequence = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,&esc,1,sizeof esc);
        constexpr guint present_no_autoload = 2 | 4;
        reply = call("setShortcut",g_variant_new("(^as@aiu)",id,sequence,present_no_autoload));
        if (!reply) return false;
        auto* actual = g_variant_get_child_value(reply,0);
        gsize n;
        auto* values = static_cast<const gint32*>(g_variant_get_fixed_array(actual,&n,sizeof esc));
        bool ok = n==1 && values[0]==esc;
        g_variant_unref(actual);
        g_variant_unref(reply);
        return ok;
    }
    bool prepare_keyboard(const std::vector<Stroke>& strokes) {
        device = open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);
        if (device<0 || ioctl(device,UI_SET_EVBIT,EV_KEY)<0) return false;
        // udev identifies a keyboard by its ordinary letter-key capabilities.
        for (int code=KEY_ESC;code<=KEY_D;++code)
            if (ioctl(device,UI_SET_KEYBIT,code)<0) return false;
        for (auto k : keys)
            if (ioctl(device,UI_SET_KEYBIT,k.linux_code)<0) return false;
        if (ioctl(device,UI_SET_KEYBIT,key.linux_code)<0) return false;
        if (!strokes.empty()) {
            if (ioctl(device,UI_SET_KEYBIT,KEY_LEFTSHIFT)<0 || ioctl(device,UI_SET_KEYBIT,KEY_RIGHTALT)<0)
                return false;
            bool enabled[248]{};
            for (auto stroke : strokes) {
                auto code=stroke.keycode-8;
                if (!enabled[code] && ioctl(device,UI_SET_KEYBIT,code)<0) return false;
                enabled[code]=true;
            }
        }
        uinput_setup setup{};
        g_strlcpy(setup.name,"Kalwer delayed keypress",sizeof setup.name);
        setup.id.bustype = BUS_VIRTUAL;
        return ioctl(device,UI_DEV_SETUP,&setup)==0 && ioctl(device,UI_DEV_CREATE)==0;
    }
public:
    std::string arm(Key chosen,void (*on_cancel)(),const std::vector<Stroke>& strokes={}) {
        reset();
        key=chosen;
        canceled=on_cancel;
        const bool wayland = g_strcmp0(g_getenv("XDG_SESSION_TYPE"),"wayland")==0 ||
            (g_getenv("WAYLAND_DISPLAY") && g_strcmp0(g_getenv("XDG_SESSION_TYPE"),"x11")!=0);
        if (wayland) {
            if (!grab_kde()) { reset(); return "Global Esc unavailable"; }
            if (!prepare_keyboard(strokes)) { reset(); return "Cannot open virtual keyboard"; }
            return {};
        }
#ifdef KALWER_HAVE_XTEST
        if (grab_x11()) return {};
#endif
        reset();
        return "Global keyboard unavailable";
    }
    bool send() {
#ifdef KALWER_HAVE_XTEST
        if (display) {
            auto* x=GDK_DISPLAY_XDISPLAY(display);
            const bool down=XTestFakeKeyEvent(x,target,True,0);
            const bool up=XTestFakeKeyEvent(x,target,False,0);
            XSync(x,False);
            return down && up;
        }
#endif
        input_event events[4]{};
        events[0].type=EV_KEY; events[0].code=key.linux_code; events[0].value=1;
        events[1].type=EV_SYN; events[1].code=SYN_REPORT;
        events[2]=events[0]; events[2].value=0;
        events[3]=events[1];
        return device>=0 && write(device,events,sizeof events)==sizeof events;
    }
    bool send(Stroke stroke) {
        auto event=[&](unsigned short code,bool pressed) {
#ifdef KALWER_HAVE_XTEST
            if (display) return bool(XTestFakeKeyEvent(GDK_DISPLAY_XDISPLAY(display),code+8,pressed,0));
#endif
            input_event events[2]{};
            events[0].type=EV_KEY; events[0].code=code; events[0].value=pressed;
            events[1].type=EV_SYN; events[1].code=SYN_REPORT;
            return device>=0 && write(device,events,sizeof events)==sizeof events;
        };
        bool ok=true;
        if(stroke.altgr) ok=event(KEY_RIGHTALT,true);
        if(stroke.shift) ok=event(KEY_LEFTSHIFT,true) && ok;
        if(ok) {
            ok=event(stroke.keycode-8,true);
            ok=event(stroke.keycode-8,false) && ok;
        }
        if(stroke.shift) ok=event(KEY_LEFTSHIFT,false) && ok;
        if(stroke.altgr) ok=event(KEY_RIGHTALT,false) && ok;
#ifdef KALWER_HAVE_XTEST
        if(display) XFlush(GDK_DISPLAY_XDISPLAY(display));
#endif
        return ok;
    }
    void release_shortcut() {
#ifdef KALWER_HAVE_XTEST
        if (display) {
            auto* x=GDK_DISPLAY_XDISPLAY(display);
            if (escape) {
                XUngrabKey(x,escape,AnyModifier,DefaultRootWindow(x));
                XFlush(x);
                gdk_window_remove_filter(nullptr,filter,this);
            }
            escape=0;
        }
#endif
        if (subscription) g_dbus_connection_signal_unsubscribe(bus,subscription);
        subscription=0;
        if (registered) {
            registered=false;
            auto* reply=call("unregister",g_variant_new("(ss)",component,action));
            if (reply) g_variant_unref(reply);
        }
        g_clear_object(&bus);
    }
    void reset() {
        release_shortcut();
#ifdef KALWER_HAVE_XTEST
        display=nullptr;
#endif
        if (device>=0) { close(device); device=-1; }
    }
};
}
