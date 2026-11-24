// Included by the GTK host after its theme and launcher drawing helpers.
namespace notice {
enum class Layout { compact, wide };
struct Card {
    GtkWidget* window = nullptr;
    std::string title, detail, icon;
    bool side=false;
    Layout layout=Layout::compact;
    double progress=0, ring=0;
    int number=0;
    void close() { if (window) gtk_widget_destroy(window); window=nullptr; }
    void draw(cairo_t* cr) const {
        int w=gtk_widget_get_allocated_width(window),h=gtk_widget_get_allocated_height(window);
        cairo_set_operator(cr,CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr,0,0,0,0); cairo_paint(cr);
        cairo_set_operator(cr,CAIRO_OPERATOR_OVER);
        cairo_translate(cr,side?w*(1-progress):0,side?0:h*(1-progress));
        double edge=state.notice_outline;
        rounded_rectangle(cr,edge/2,edge/2,w-edge,h-edge+(side?0:8),8);
        theme_source_rgba(cr,0,.075,.043,.975); cairo_fill_preserve(cr);
        theme_source_rgba(cr,.31,.68,.47,.86); cairo_set_line_width(cr,edge); cairo_stroke(cr);
        if (side) {
            cairo_set_line_width(cr,2.5);
            cairo_arc(cr,33,37,20,0,2*G_PI);
            theme_source_rgba(cr,.16,.29,.21,1); cairo_stroke(cr);
            if (ring>0) {
                cairo_arc(cr,33,37,20,-G_PI/2,-G_PI/2+2*G_PI*ring);
                theme_source_rgba(cr,.53,.90,.66,1); cairo_stroke(cr);
            }
            draw_layout(cr,number?std::to_string(number):icon,26,24,"JetBrains Mono Bold 13",.80,.94,.84);
            draw_layout(cr,title,65,16,"JetBrains Mono SemiBold 11",.80,.94,.84);
            if (number) {
                rounded_rectangle(cr,65,41,31,17,3);
                theme_source_rgba(cr,.40,.54,.45,1); cairo_set_line_width(cr,1); cairo_stroke(cr);
                draw_layout(cr,"Esc",69,41,"JetBrains Mono 8",.80,.94,.84);
                draw_layout(cr,"to cancel",102,40,"JetBrains Mono 9",.66,.79,.70);
            } else draw_layout(cr,detail,65,40,"JetBrains Mono 9",.66,.79,.70);
        } else {
            if (icon=="timer") {
                cairo_set_line_width(cr,1.8); theme_source_rgba(cr,.65,.92,.74,1);
                cairo_arc(cr,24,24,8,0,2*G_PI); cairo_stroke(cr);
                cairo_move_to(cr,21,12); cairo_line_to(cr,27,12);
                cairo_move_to(cr,24,12); cairo_line_to(cr,24,16);
                cairo_move_to(cr,24,19); cairo_line_to(cr,24,24); cairo_line_to(cr,28,26); cairo_stroke(cr);
            } else if (custom_icon) {
                int iw=gdk_pixbuf_get_width(custom_icon),ih=gdk_pixbuf_get_height(custom_icon);
                double ratio=22.0/std::max(iw,ih);
                cairo_save(cr); cairo_translate(cr,24-iw*ratio/2,23-ih*ratio/2);
                cairo_scale(cr,ratio,ratio); gdk_cairo_set_source_pixbuf(cr,custom_icon,0,0);
                cairo_paint(cr); cairo_restore(cr);
            } else draw_icon(cr,"kalwer",13,12,22);
            PangoLayout* text=pango_cairo_create_layout(cr);
            auto* font=pango_font_description_from_string("JetBrains Mono SemiBold 12");
            pango_layout_set_font_description(text,font); pango_font_description_free(font);
            pango_layout_set_text(text,title.c_str(),-1);
            int left=layout==Layout::wide?42:46;
            pango_layout_set_width(text,(w-left-(layout==Layout::wide?left:10))*PANGO_SCALE);
            pango_layout_set_alignment(text,layout==Layout::wide?PANGO_ALIGN_CENTER:PANGO_ALIGN_LEFT);
            pango_layout_set_ellipsize(text,PANGO_ELLIPSIZE_END);
            int height; pango_layout_get_pixel_size(text,nullptr,&height);
            cairo_move_to(cr,left,(h-height)/2);
            theme_source_rgba(cr,.80,.94,.84,1); pango_cairo_show_layout(cr,text); g_object_unref(text);
        }
    }
    GdkPixbuf* custom_icon=nullptr;
    void open(GdkMonitor* monitor,int x,int y,int width,int height) {
        close();
        window=gtk_window_new(GTK_WINDOW_POPUP);
        auto* win=GTK_WINDOW(window);
        gtk_window_set_title(win,side?"Kalwer countdown":"Kalwer notification");
        gtk_window_set_accept_focus(win,FALSE); gtk_window_set_focus_on_map(win,FALSE);
        gtk_window_set_keep_above(win,TRUE);
        gtk_widget_set_app_paintable(window,TRUE);
        if (auto* visual=gdk_screen_get_rgba_visual(gtk_widget_get_screen(window))) gtk_widget_set_visual(window,visual);
        gtk_widget_set_size_request(window,width,height);
#ifdef KALWER_HAVE_LAYER_SHELL
        if (gtk_layer_is_supported()) {
            gtk_layer_init_for_window(win); gtk_layer_set_monitor(win,monitor);
            gtk_layer_set_layer(win,GTK_LAYER_SHELL_LAYER_OVERLAY);
            gtk_layer_set_keyboard_mode(win,GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
            gtk_layer_set_anchor(win,GTK_LAYER_SHELL_EDGE_LEFT,TRUE);
            gtk_layer_set_anchor(win,GTK_LAYER_SHELL_EDGE_TOP,TRUE);
            GdkRectangle bounds{}; gdk_monitor_get_geometry(monitor,&bounds);
            gtk_layer_set_margin(win,GTK_LAYER_SHELL_EDGE_LEFT,x-bounds.x);
            gtk_layer_set_margin(win,GTK_LAYER_SHELL_EDGE_TOP,y-bounds.y);
            gtk_layer_set_exclusive_zone(win,-1);
        } else
#endif
        gtk_window_move(win,x,y);
        (void)monitor;
        g_signal_connect(window,"draw",G_CALLBACK(+[](GtkWidget*,cairo_t* cr,gpointer self)->gboolean {
            static_cast<Card*>(self)->draw(cr); return TRUE;
        }),this);
        gtk_widget_realize(window);
        gdk_window_set_pass_through(gtk_widget_get_window(window),TRUE);
        gtk_widget_show(window);
    }
};
struct Timer {
    Card top,side;
    kalwer::delaypress::Input input;
    GdkRectangle area{};
    GdkMonitor* monitor=nullptr;
    gint64 start=0,deadline=0,ended=0;
    guint source=0;
    double dwell=0;
    bool hide_after=false;
    std::string key;
    void clear() {
        if(source) g_source_remove(source);
        source=0; deadline=0; ended=0; hide_after=false;
        top.close(); side.close(); g_clear_object(&top.custom_icon);
        input.reset(); g_clear_object(&monitor);
    }
    void hide_parent() {
        top.close();
        if (!hide_after) return;
        hide_after=false;
        stop_query(); state.opening=state.closing=false;
        state.hidden_us=g_get_monotonic_time();
        gtk_widget_hide(state.window);
    }
    void outcome(bool cancel) {
        if (!deadline) return;
        deadline=0;
        bool sent=!cancel && input.send();
        input.release_shortcut();
        ended=g_get_monotonic_time();
        side.title=cancel?"Canceled":sent?key+" sent":"Keypress failed";
        side.detail=cancel?key+" skipped":"";
        side.icon=cancel?"×":sent?"✓":"!";
        hide_parent();
    }
    void update() {
        gint64 now=g_get_monotonic_time();
        double age=(now-start)/1e6;
        if (top.window) {
            top.progress=std::min(ease(age/.20),ease((dwell-age)/.17));
            if (deadline) top.title=key+" in "+std::to_string(std::max(1,int(std::ceil((deadline-now)/1e6))))+" s";
            if (age>=dwell || !gtk_widget_get_visible(state.window)) hide_parent();
            else gtk_widget_queue_draw(top.window);
        }
        if (deadline && now>=deadline) {
            // A user may have reopened the launcher during a long countdown.
            outcome(gtk_window_is_active(GTK_WINDOW(state.window)) && gtk_widget_get_visible(state.window));
        }
        if (deadline || ended) {
            double left=(deadline-now)/1e6;
            if (ended || left<=3) {
                if (!side.window) {
                    side.side=true;
                    side.open(monitor,area.x+area.width-248,area.y+(area.height-74)/2,236,74);
                }
                if (ended) {
                    side.number=0; side.ring=1;
                    side.progress=ease((1.15-(now-ended)/1e6)/.18);
                } else {
                    side.title="Pressing "+key;
                    side.number=std::max(1,int(std::ceil(left))); side.ring=std::clamp(left/3.,0.,1.);
                    side.progress=ease(std::min(age,3-left)/.18);
                }
                gtk_widget_queue_draw(side.window);
            }
        }
    }
    static double ease(double v) { return (1-std::cos(G_PI*std::clamp(v,0.,1.)))/2; }
    void open_top(Layout layout,const std::string& message,const std::string& image) {
        top.layout=layout; top.title=message; top.icon=image; top.progress=0;
        if (!image.empty() && image!="timer" && image!="kalwer") {
            if (g_path_is_absolute(image.c_str()))
                top.custom_icon=gdk_pixbuf_new_from_file_at_scale(image.c_str(),64,64,TRUE,nullptr);
            else top.custom_icon=gtk_icon_theme_load_icon(state.icon_theme,image.c_str(),48,GTK_ICON_LOOKUP_FORCE_SIZE,nullptr);
        }
        int x,y; gtk_window_get_position(GTK_WINDOW(state.window),&x,&y);
#ifdef KALWER_HAVE_LAYER_SHELL
        if (gtk_layer_is_supported()) {
            x=area.x+(area.width-kWindowWidth)/2; y=area.y+(area.height-kWindowHeight)/2;
        }
#endif
        int width=layout==Layout::wide?kSearchWidth-28:300;
        x=std::clamp(x+int(kSearchX)+14,area.x,std::max(area.x,area.x+area.width-width));
        y=std::max(area.y,y+int(kSearchY)+update_banner.offset()-46);
        top.open(monitor,x,y,width,46);
    }
};
inline Timer timer;
inline gboolean tick(gpointer) {
    timer.source=0;
    timer.update();
    gint64 now=g_get_monotonic_time();
    if (!timer.deadline && !timer.top.window && (!timer.ended || now-timer.ended>1150000)) {
        timer.clear();
        if (!state.resident && !gtk_widget_get_visible(state.window) && !state.settings_window && !state.output_window)
            g_application_quit(G_APPLICATION(state.app));
        return G_SOURCE_REMOVE;
    }
    guint next=16;
    if (!timer.top.window && timer.deadline-now>3000000)
        next=std::max<gint64>(1,(timer.deadline-now-3000000)/1000);
    timer.source=g_timeout_add(next,tick,nullptr);
    return G_SOURCE_REMOVE;
}
inline void cancel() {
    timer.outcome(true);
    if (timer.source) g_source_remove(timer.source);
    timer.source=g_timeout_add(1,tick,nullptr);
}
inline void start(const std::string& command) {
    if (timer.deadline && timer.hide_after) return;
    if (timer.deadline && !kalwer::delaypress::parse(command)) return;
    timer.clear();
    timer.start=g_get_monotonic_time();
    timer.monitor=kalwer::popup_monitor(GTK_WINDOW(state.window));
    if (!timer.monitor) return;
    g_object_ref(timer.monitor); gdk_monitor_get_workarea(timer.monitor,&timer.area);
    auto request=kalwer::delaypress::parse(command);
    std::string error=request?timer.input.arm(request->key,cancel):"Use /delaypress enter 20s";
    if (!error.empty()) {
        timer.dwell=2.4; timer.open_top(Layout::wide,error,"kalwer");
    } else {
        timer.key=request->key.name; timer.deadline=timer.start+gint64(request->seconds)*G_USEC_PER_SEC;
        timer.hide_after=true; timer.dwell=std::min(1.65,request->seconds-.40);
        timer.open_top(state.notice_wide?Layout::wide:Layout::compact,"",state.notice_icon);
    }
    timer.update();
    timer.source=g_timeout_add(16,tick,nullptr);
}
}
