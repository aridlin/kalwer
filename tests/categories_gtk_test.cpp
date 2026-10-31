#define main kalwer_application_main
#include "../main.cpp"
#undef main
#include <cassert>
#include <iostream>
void settle_category(){auto until=g_get_monotonic_time()+300000;while(g_get_monotonic_time()<until){while(g_main_context_iteration(nullptr,FALSE)){}g_usleep(5000);}}
int main(int argc,char** argv) {
    gtk_init(&argc,&argv);
    auto* app=gtk_application_new("pl.aridlin.kalwer.categories.test",G_APPLICATION_NON_UNIQUE);
    assert(g_application_register(G_APPLICATION(app),nullptr,nullptr));activate(app,nullptr);
    gtk_entry_set_text(GTK_ENTRY(state.entry),"&");settle_category();assert(state.results.size()>=9);
    assert(state.results[0].provider=="kalwer-category");activate_selection();assert(std::string(gtk_entry_get_text(GTK_ENTRY(state.entry)))=="&games");
    gtk_entry_set_text(GTK_ENTRY(state.entry),"&games minesweeper");settle_category();
    assert(std::any_of(state.results.begin(),state.results.end(),[](const auto& r){return r.identifier=="/minesweeper";}));
    auto cats=kalwer::categories::load(kalwer::categories::config_path());cats.push_back({"manual-test","",false,{"/chess","/minesweeper"}});
    GError* error=nullptr;assert(kalwer::categories::save(kalwer::categories::config_path(),cats,&error));
    gtk_entry_set_text(GTK_ENTRY(state.entry),"&manual-test");settle_category();assert(state.results[0].identifier=="/chess");assert(state.results[1].identifier=="/minesweeper");
    gtk_entry_set_text(GTK_ENTRY(state.entry),"&games");settle_category();
    if(argc>1){auto* pix=gdk_pixbuf_get_from_window(gtk_widget_get_window(state.window),0,0,kWindowWidth,kWindowHeight);assert(pix);assert(gdk_pixbuf_save(pix,argv[1],"png",nullptr,nullptr));g_object_unref(pix);}
    g_timeout_add(400,+[](gpointer output)->gboolean {auto* all=gtk_window_list_toplevels();for(auto* i=all;i;i=i->next)if(GTK_IS_DIALOG(i->data)&&std::string(gtk_window_get_title(GTK_WINDOW(i->data)))=="Kalwer categories"){
        if(output){auto* w=GTK_WIDGET(i->data);auto* pix=gdk_pixbuf_get_from_window(gtk_widget_get_window(w),0,0,gtk_widget_get_allocated_width(w),gtk_widget_get_allocated_height(w));assert(pix);assert(gdk_pixbuf_save(pix,static_cast<const char*>(output),"png",nullptr,nullptr));g_object_unref(pix);}
        gtk_dialog_response(GTK_DIALOG(i->data),GTK_RESPONSE_CANCEL);
    }g_list_free(all);return G_SOURCE_REMOVE;},argc>2?argv[2]:nullptr);
    kalwer::categories::edit(GTK_WINDOW(state.window),"games");
    std::cout<<"Category listing, activation, filtering, persisted manual order and GTK editor passed\n";
}
