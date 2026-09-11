#include "../multiline_gtk.hpp"
#include <cassert>
#include <iostream>
int main(int argc,char** argv){
    gtk_init(&argc,&argv);
    GtkWidget* view=gtk_text_view_new();g_object_ref_sink(view);kalwer::TextHistory::attach(view);
    auto* buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buffer,"żółw\nsecond",-1);
    GtkTextIter a,b;gtk_text_buffer_get_iter_at_offset(buffer,&a,2);gtk_text_buffer_get_iter_at_offset(buffer,&b,7);
    gtk_text_buffer_select_range(buffer,&a,&b);
    gtk_text_buffer_begin_user_action(buffer);gtk_text_buffer_delete_selection(buffer,TRUE,TRUE);gtk_text_buffer_insert_at_cursor(buffer,"🐍\nnew",-1);gtk_text_buffer_end_user_action(buffer);
    auto* history=static_cast<kalwer::TextHistory*>(g_object_get_data(G_OBJECT(buffer),"kalwer-history"));
    auto edited=kalwer::TextHistory::read(buffer);assert(edited.text=="żó🐍\nnewcond");
    assert(history->restore(GTK_TEXT_VIEW(view),false));auto restored=kalwer::TextHistory::read(buffer);
    assert(restored.text=="żółw\nsecond" && restored.cursor==2 && restored.anchor==7);
    assert(history->restore(GTK_TEXT_VIEW(view),true));assert(kalwer::TextHistory::read(buffer).text==edited.text);
    history->restore(GTK_TEXT_VIEW(view),false);
    gtk_text_buffer_begin_user_action(buffer);gtk_text_buffer_delete_selection(buffer,TRUE,TRUE);gtk_text_buffer_insert_at_cursor(buffer,"replacement",-1);gtk_text_buffer_end_user_action(buffer);
    assert(history->redo.empty());
    gtk_text_buffer_set_text(buffer,"fresh query",-1);assert(history->undo.empty());
    gtk_widget_destroy(view);g_object_unref(view);
    std::cout<<"Multiline Unicode selection, grouped undo/redo, branching edit and query reset passed\n";
}
