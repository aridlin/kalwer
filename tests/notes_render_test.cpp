#define main kalwer_application_main
#include "../main.cpp"
#undef main
#include <cassert>
#include <iostream>
void settle(){auto until=g_get_monotonic_time()+1500000;while(g_get_monotonic_time()<until){while(g_main_context_iteration(nullptr,FALSE)){}g_usleep(5000);}}
int main(int argc,char** argv){
    gtk_init(&argc,&argv);
    auto app=gtk_application_new("pl.aridlin.kalwer.notes.probe",G_APPLICATION_NON_UNIQUE);
    assert(g_application_register(G_APPLICATION(app),nullptr,nullptr));
    activate(app,nullptr);
    gtk_entry_set_text(GTK_ENTRY(state.entry),"*task remember to test sync");settle();
    assert(state.results.size()==1 && state.results[0].action=="save");
    assert(!update_ready && !update_status.checking());
    assert(update_status.get().find("paused")!=std::string::npos);
    if(argc>1) {
    auto pix=gdk_pixbuf_get_from_window(gtk_widget_get_window(state.window),0,0,kWindowWidth,kWindowHeight);
    assert(pix);assert(gdk_pixbuf_save(pix,argv[1],"png",nullptr,nullptr));g_object_unref(pix);
    }
    notes_loaded=true;notes_loaded_us=g_get_monotonic_time();
    notes_cache={{"probe","Read-back example","Sample note body","markdown","someday","active"}};
    gtk_entry_set_text(GTK_ENTRY(state.entry),"*read");settle();
    assert(state.results.size()==1 && state.results[0].action=="read");
    // Reopen the retained query and check it stays in Notes mode.
    state.hidden_us=g_get_monotonic_time();show_popup();settle();
    assert(state.results.size()==1 && state.results[0].provider=="kalwer-notes");
    std::cout<<"Rendered task capture, read list, retained query and local update protection passed\n";
    return 0;
}
