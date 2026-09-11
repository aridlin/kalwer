#pragma once
#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <utility>

namespace kalwer {
// GTK 3 TextView supplies native editing but has no built-in undo stack.
// Record complete user actions, retaining selection and Unicode cursor offsets.
struct TextHistory {
    struct Edit {std::string text;int cursor=0,anchor=0;};
    std::vector<Edit> undo,redo;
    Edit before;
    unsigned depth=0;
    bool restoring=false;
    static Edit read(GtkTextBuffer* buffer){
        GtkTextIter a,b;gtk_text_buffer_get_bounds(buffer,&a,&b);
        gchar* raw=gtk_text_buffer_get_text(buffer,&a,&b,FALSE);Edit edit;edit.text=raw;g_free(raw);
        gtk_text_buffer_get_iter_at_mark(buffer,&a,gtk_text_buffer_get_insert(buffer));edit.cursor=gtk_text_iter_get_offset(&a);
        gtk_text_buffer_get_iter_at_mark(buffer,&b,gtk_text_buffer_get_selection_bound(buffer));edit.anchor=gtk_text_iter_get_offset(&b);
        return edit;
    }
    static void push(std::vector<Edit>& stack,Edit edit){
        if(edit.text.size()>4*1024*1024){stack.clear();return;}
        stack.push_back(std::move(edit));size_t bytes=0;for(const auto& item:stack)bytes+=item.text.size();
        while(stack.size()>128 || bytes>4*1024*1024){bytes-=stack.front().text.size();stack.erase(stack.begin());}
    }
    bool restore(GtkTextView* view,bool forward){
        auto& source=forward?redo:undo;auto& destination=forward?undo:redo;
        if(source.empty())return true;
        auto* buffer=gtk_text_view_get_buffer(view);push(destination,read(buffer));
        Edit edit=std::move(source.back());source.pop_back();restoring=true;
        gtk_text_buffer_set_text(buffer,edit.text.c_str(),-1);
        GtkTextIter a,b;gtk_text_buffer_get_iter_at_offset(buffer,&a,edit.cursor);gtk_text_buffer_get_iter_at_offset(buffer,&b,edit.anchor);
        gtk_text_buffer_select_range(buffer,&a,&b);restoring=false;
        gtk_text_view_scroll_mark_onscreen(view,gtk_text_buffer_get_insert(buffer));return true;
    }
    static void attach(GtkWidget* view){
        auto* buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));auto* history=new TextHistory;
        g_object_set_data_full(G_OBJECT(buffer),"kalwer-history",history,+[](gpointer p){delete static_cast<TextHistory*>(p);});
        g_signal_connect(buffer,"begin-user-action",G_CALLBACK(+[](GtkTextBuffer* b,gpointer p){auto& h=*static_cast<TextHistory*>(p);if(h.depth++==0)h.before=read(b);}),history);
        g_signal_connect(buffer,"end-user-action",G_CALLBACK(+[](GtkTextBuffer* b,gpointer p){auto& h=*static_cast<TextHistory*>(p);if(h.depth && --h.depth==0 && !h.restoring && h.before.text!=read(b).text){push(h.undo,std::move(h.before));h.redo.clear();}}),history);
        g_signal_connect(buffer,"changed",G_CALLBACK(+[](GtkTextBuffer*,gpointer p){auto& h=*static_cast<TextHistory*>(p);if(!h.depth && !h.restoring){h.undo.clear();h.redo.clear();}}),history);
        g_signal_connect(view,"key-press-event",G_CALLBACK(+[](GtkWidget* w,GdkEventKey* e,gpointer p)->gboolean{
            if(!(e->state&GDK_CONTROL_MASK))return FALSE;
            guint key=gdk_keyval_to_lower(e->keyval);if(key!=GDK_KEY_z && key!=GDK_KEY_y)return FALSE;
            return static_cast<TextHistory*>(p)->restore(GTK_TEXT_VIEW(w),key==GDK_KEY_y || (e->state&GDK_SHIFT_MASK));
        }),history);
    }
};
}
