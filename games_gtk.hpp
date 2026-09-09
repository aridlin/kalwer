#pragma once
#include "games.hpp"
#include "live_backdrop.hpp"
#include <gtk/gtk.h>
#include "gpu_game.hpp"
#include "gpu_dither.hpp"
#include <functional>
namespace kalwer::games {
struct GtkGame { Game game; GpuPainter painter; kalwer::GpuDither dither; std::function<std::array<double,3>()> animation;std::function<void()> close;gint64 last=0; guint timer=0; ~GtkGame(){if(timer)g_source_remove(timer);} explicit GtkGame(Kind kind):game(kind){} };
inline Game* canvas_game(GtkWidget* canvas) {
    return &static_cast<GtkGame*>(g_object_get_data(G_OBJECT(canvas), "kalwer-game"))->game;
}
inline GtkWidget* create_game_canvas(GtkWidget* window, Kind kind,std::function<std::array<double,3>()> animation={},std::function<void()> close={}) {
    auto* session=new GtkGame(kind);session->animation=std::move(animation);session->close=std::move(close);
    GtkWidget* canvas=gtk_gl_area_new();
    gtk_gl_area_set_required_version(GTK_GL_AREA(canvas),4,3);
    gtk_gl_area_set_has_alpha(GTK_GL_AREA(canvas),TRUE);
    g_signal_connect(canvas,"unrealize",G_CALLBACK(+[](GtkGLArea* area,gpointer data){gtk_gl_area_make_current(area);if(!gtk_gl_area_get_error(area)){auto* s=static_cast<GtkGame*>(data);s->painter.release();s->dither.release();}}),session);
    g_object_set_data_full(G_OBJECT(canvas),"kalwer-game",session,+[](gpointer p){delete static_cast<GtkGame*>(p);});
    gtk_widget_set_can_focus(canvas,TRUE);
    gtk_widget_set_hexpand(canvas,TRUE); gtk_widget_set_vexpand(canvas,TRUE);
    gtk_widget_add_events(canvas,GDK_BUTTON_PRESS_MASK|GDK_POINTER_MOTION_MASK);
    g_signal_connect(canvas,"render",G_CALLBACK((+[](GtkGLArea* area,GdkGLContext*,gpointer data)->gboolean {
        auto* s=static_cast<GtkGame*>(data);auto& painter=s->painter;
        if(gtk_gl_area_get_error(area) || (!painter.program && !painter.init()))return TRUE;
        GtkWidget* widget=GTK_WIDGET(area);int scale=gtk_widget_get_scale_factor(widget);
        glViewport(0,0,gtk_widget_get_allocated_width(widget)*scale,gtk_widget_get_allocated_height(widget)*scale);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
        painter.logical_width=320;painter.logical_height=378;painter.origin_y=0;
        auto phases=s->animation?s->animation():std::array<double,3>{1,1,1};double unfold=phases[2];
        double left=18*phases[1],bottom=18+352*unfold;
        float sx=gtk_widget_get_allocated_width(widget)*scale/320.f,sy=gtk_widget_get_allocated_height(widget)*scale/378.f;
        glEnable(GL_SCISSOR_TEST);glScissor(int(left*sx),int((378-bottom)*sy),int((312-left)*sx),std::max(0,int((bottom-18)*sy)));
        if(unfold>0 && s->dither.update(kalwer::live_backdrop.copy(),kalwer::appearance.popup_mode)) {
            glUseProgram(painter.program);glUniform2f(glGetUniformLocation(painter.program,"backdropSize"),s->dither.width,s->dither.height);
            unsigned dark=kalwer::appearance.bw || kalwer::appearance.popup_mode==5?0:kalwer::themes[kalwer::appearance.theme].background;
            glUniform3f(glGetUniformLocation(painter.program,"dark"),((dark>>16)&255)/255.f,((dark>>8)&255)/255.f,(dark&255)/255.f);
            painter.backdrop=s->dither.output;painter.quad(0,0,320,378,0xffffff,.8,4);painter.flush();
        }
        if(unfold>0){
            // One GL surface for the board, title and border avoids GTK's mixed Cairo/GL copies.
            glScissor(int(19*sx),int((378-bottom+1)*sy),int(292*sx),std::max(0,int((bottom-(18+41*unfold)-1)*sy)));
            s->game.draw(painter,true);
            for(auto& v:painter.vertices){v.x=19+v.x*292/420;v.y=18+(41+(v.y-38)*310/452)*unfold;}
            painter.flush();
            glScissor(int(left*sx),int((378-bottom)*sy),int((312-left)*sx),std::max(0,int((bottom-18)*sy)));
            painter.quad(19,19,292,40*unfold,0x081a18,.84,0);
            if(!kalwer::appearance.popup_mode || kalwer::appearance.popup_keep_halftone)painter.quad(19,19,292,40*unfold,0x081a18,.4,3,19,19,292,40);
            auto first=painter.vertices.size();
            painter.text(25,44,9,s->game.kind==Kind::peggle?"peggle":s->game.kind==Kind::snake?"snake":"minesweeper",0xe0f5e8);
            painter.text(244,44,8,"GAME",0x8ce9b3);
            painter.quad(273,28,32,25,0x112e29,.9,0);
            painter.line(273,28,305,28,0x8ce9b3,1);painter.line(305,28,305,53,0x8ce9b3,1);painter.line(305,53,273,53,0x8ce9b3,1);painter.line(273,53,273,28,0x8ce9b3,1);
            painter.line(287,38,291,42,0xe0f5e8,1);painter.line(291,38,287,42,0xe0f5e8,1);
            for(size_t i=first;i<painter.vertices.size();++i)painter.vertices[i].y=18+(painter.vertices[i].y-18)*unfold;
            painter.flush();
        }
        glDisable(GL_SCISSOR_TEST);
        if(unfold>0){painter.line(left,18,left,bottom,0x8ce9b3,1.5);painter.line(left,bottom,312,bottom,0x8ce9b3,1.5);painter.line(312,bottom,312,18,0x8ce9b3,1.5);}
        painter.line(left,18,left+294*phases[0],18,0x8ce9b3,1.7);painter.flush();return TRUE;
    })),session);
    g_signal_connect_object(window,"notify::is-active",G_CALLBACK(+[](GObject* window,GParamSpec*,gpointer data) {
        auto* canvas=GTK_WIDGET(data);
        auto* s=static_cast<GtkGame*>(g_object_get_data(G_OBJECT(canvas),"kalwer-game"));
        s->game.focused=gtk_window_is_active(GTK_WINDOW(window)); s->last=g_get_monotonic_time();
        gtk_gl_area_queue_render(GTK_GL_AREA(canvas));
    }),canvas,G_CONNECT_DEFAULT);
    g_signal_connect(canvas,"button-press-event",G_CALLBACK(+[](GtkWidget* widget,GdkEventButton* e,gpointer data)->gboolean {
        auto* s=static_cast<GtkGame*>(data);
        double x=e->x*320/gtk_widget_get_allocated_width(widget);double y=e->y*378/gtk_widget_get_allocated_height(widget);
        if(s->animation && s->animation()[2]<.999)return TRUE;
        if(x>=273 && x<=305 && y>=28 && y<=53){if(s->close)s->close();return TRUE;}
        s->game.pointer((x-19)*420/292,38+(y-59)*452/310,e->button);
        gtk_widget_grab_focus(widget); gtk_gl_area_queue_render(GTK_GL_AREA(widget)); return TRUE;
    }),session);
    g_signal_connect(canvas,"motion-notify-event",G_CALLBACK(+[](GtkWidget* widget,GdkEventMotion* e,gpointer data)->gboolean {
        auto* s=static_cast<GtkGame*>(data);
        if(s->game.kind==Kind::peggle) { s->game.pointer((e->x*320/gtk_widget_get_allocated_width(widget)-19)*420/292,38+(e->y*378/gtk_widget_get_allocated_height(widget)-59)*452/310,0); gtk_gl_area_queue_render(GTK_GL_AREA(widget)); } return TRUE;
    }),session);
    session->timer=g_timeout_add(16,+[](gpointer data)->gboolean {
        auto* widget=GTK_WIDGET(data);auto* s=static_cast<GtkGame*>(g_object_get_data(G_OBJECT(widget),"kalwer-game"));
        auto now=g_get_monotonic_time();
        if(s->game.focused && s->last){
            int seconds=int(s->game.elapsed);auto head=s->game.snake.front();bool over=s->game.over;
            s->game.tick((now-s->last)/1000000.);
            bool moving=s->game.kind==Kind::peggle || !(head==s->game.snake.front()) || seconds!=int(s->game.elapsed) || over!=s->game.over || (s->game.won && s->game.celebration<3);
            if(moving)gtk_gl_area_queue_render(GTK_GL_AREA(widget));
        }
        s->last=now;return G_SOURCE_CONTINUE;
    },canvas);
    return canvas;
}
}
