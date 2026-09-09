#pragma once
#include "games.hpp"
#include <gtk/gtk.h>
namespace kalwer::games {
struct CairoPainter {
    cairo_t* cr;
    void color(unsigned c) { cairo_set_source_rgb(cr,((c>>16)&255)/255.,((c>>8)&255)/255.,(c&255)/255.); }
    void rect(double x,double y,double w,double h,unsigned c) { color(c); cairo_rectangle(cr,x,y,w,h); cairo_fill(cr); }
    void circle(double x,double y,double r,unsigned c) { color(c); cairo_arc(cr,x,y,r,0,6.2831853); cairo_fill(cr); }
    void line(double x,double y,double a,double b,unsigned c,double width) { color(c); cairo_set_line_width(cr,width); cairo_move_to(cr,x,y); cairo_line_to(cr,a,b); cairo_stroke(cr); }
    void text(double x,double y,double size,const std::string& s,unsigned c) { color(c); cairo_select_font_face(cr,"sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL); cairo_set_font_size(cr,size); cairo_move_to(cr,x,y); cairo_show_text(cr,s.c_str()); }
};
struct GtkGame { Game game; gint64 last=0; explicit GtkGame(Kind kind):game(kind){} };
inline Game* canvas_game(GtkWidget* canvas) {
    return &static_cast<GtkGame*>(g_object_get_data(G_OBJECT(canvas), "kalwer-game"))->game;
}
inline GtkWidget* create_game_canvas(GtkWidget* window, Kind kind) {
    auto* session=new GtkGame(kind);
    GtkWidget* canvas=gtk_drawing_area_new();
    g_object_set_data_full(G_OBJECT(canvas),"kalwer-game",session,+[](gpointer p){delete static_cast<GtkGame*>(p);});
    gtk_widget_set_can_focus(canvas,TRUE);
    gtk_widget_set_hexpand(canvas,TRUE); gtk_widget_set_vexpand(canvas,TRUE);
    gtk_widget_add_events(canvas,GDK_BUTTON_PRESS_MASK|GDK_POINTER_MOTION_MASK);
    g_signal_connect(canvas,"draw",G_CALLBACK(+[](GtkWidget* widget,cairo_t* cr,gpointer data)->gboolean {
        auto* s=static_cast<GtkGame*>(data);
        cairo_scale(cr,gtk_widget_get_allocated_width(widget)/420.,gtk_widget_get_allocated_height(widget)/452.);
        cairo_translate(cr,0,-38);
        CairoPainter painter{cr}; s->game.draw(painter,true); return TRUE;
    }),session);
    g_signal_connect_object(window,"notify::is-active",G_CALLBACK(+[](GObject* window,GParamSpec*,gpointer data) {
        auto* canvas=GTK_WIDGET(data);
        auto* s=static_cast<GtkGame*>(g_object_get_data(G_OBJECT(canvas),"kalwer-game"));
        s->game.focused=gtk_window_is_active(GTK_WINDOW(window)); s->last=g_get_monotonic_time();
        gtk_widget_queue_draw(canvas);
    }),canvas,G_CONNECT_DEFAULT);
    g_signal_connect(canvas,"button-press-event",G_CALLBACK(+[](GtkWidget* widget,GdkEventButton* e,gpointer data)->gboolean {
        auto* s=static_cast<GtkGame*>(data);
        s->game.pointer(e->x*420/gtk_widget_get_allocated_width(widget),38+e->y*452/gtk_widget_get_allocated_height(widget),e->button);
        gtk_widget_grab_focus(widget); gtk_widget_queue_draw(widget); return TRUE;
    }),session);
    g_signal_connect(canvas,"motion-notify-event",G_CALLBACK(+[](GtkWidget* widget,GdkEventMotion* e,gpointer data)->gboolean {
        auto* s=static_cast<GtkGame*>(data);
        if(s->game.kind==Kind::peggle) { s->game.pointer(e->x*420/gtk_widget_get_allocated_width(widget),38+e->y*452/gtk_widget_get_allocated_height(widget),0); gtk_widget_queue_draw(widget); } return TRUE;
    }),session);
    gtk_widget_add_tick_callback(canvas,+[](GtkWidget* widget,GdkFrameClock*,gpointer data)->gboolean {
        auto* s=static_cast<GtkGame*>(data); auto now=g_get_monotonic_time();
        if(s->game.focused && s->last) { s->game.tick((now-s->last)/1000000.); gtk_widget_queue_draw(widget); }
        s->last=now; return G_SOURCE_CONTINUE;
    },session,nullptr);
    return canvas;
}
}
