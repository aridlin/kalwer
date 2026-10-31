#define main kalwer_application_main
#include "../main.cpp"
#undef main
#include <cassert>
#include <iostream>
int main(int argc,char** argv) {
    gtk_init(&argc,&argv);
    state.app=gtk_application_new("pl.aridlin.kalwer.notes.test",G_APPLICATION_NON_UNIQUE);
    assert(g_application_register(G_APPLICATION(state.app),nullptr,nullptr));
    state.window=gtk_application_window_new(state.app);
    state.entry=gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(state.window),state.entry);
    notes_loaded=true;notes_loaded_us=g_get_monotonic_time();
    notes_cache={{"one","Żółw","Text\nfull body","markdown","someday","active"},{"two","Task","[ ] test","checklist","soon","active"},{"three","Archived","old","markdown","reference","archived"}};
    gtk_entry_set_text(GTK_ENTRY(state.entry),"*read ŻÓŁW");notes_results();
    assert(state.results.size()==1 && state.results[0].identifier=="one");
    gtk_entry_set_text(GTK_ENTRY(state.entry),"*task");notes_results();
    assert(state.results.size()==1 && state.results[0].identifier=="two");
    gtk_entry_set_text(GTK_ENTRY(state.entry),"*someday");notes_results();
    assert(state.results.size()==1 && state.results[0].identifier=="one");
    gtk_entry_set_text(GTK_ENTRY(state.entry),"*someday keep this text");notes_results();
    assert(state.results.size()==1 && state.results[0].action=="save");
    notes_auth=true;notes_results();assert(state.results[0].action=="login");
    gtk_entry_set_text(GTK_ENTRY(state.entry),"*web");notes_results();assert(state.results[0].action=="web");
    notes_login();assert(notes_login_window);
    auto password=GTK_ENTRY(g_object_get_data(G_OBJECT(notes_login_window),"password"));assert(!gtk_entry_get_visibility(password));
    gtk_dialog_response(GTK_DIALOG(notes_login_window),GTK_RESPONSE_CANCEL);assert(!notes_login_window);
    notes_auth=false;notes_error="offline";gtk_entry_set_text(GTK_ENTRY(state.entry),"*someday keep this text");notes_results();
    assert(state.results[0].action=="retry");assert(std::string(gtk_entry_get_text(GTK_ENTRY(state.entry)))=="*someday keep this text");
    notes_error.clear();notes_busy=true;notes_results();assert(state.results[0].action=="info");notes_busy=false;
    gtk_widget_destroy(state.window);g_object_unref(state.app);
    std::cout<<"GTK notes list, Unicode search, filters, account dialog, busy state and preserved input passed\n";
}
