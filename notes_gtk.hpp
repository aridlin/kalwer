// Included inside the Linux host namespace after show_local_results().
kalwer::notes::Client notes_client;
std::vector<kalwer::notes::Note> notes_cache;
bool notes_busy=false,notes_loaded=false,notes_auth=false;
gint64 notes_loaded_us=0;
std::string notes_error;
GtkWidget* notes_login_window=nullptr;
void notes_results();
struct NotesWork {kalwer::notes::Command command;std::string input,username,password;kalwer::notes::Reply reply;};
void notes_start(kalwer::notes::Command command,std::string username={},std::string password={}) {
    if(notes_busy)return;
    notes_busy=true;notes_error.clear();
    auto work=new NotesWork{command,gtk_entry_get_text(GTK_ENTRY(state.entry)),std::move(username),std::move(password),{}};
    auto task=g_task_new(nullptr,nullptr,[](GObject*,GAsyncResult* result,gpointer){
        auto w=static_cast<NotesWork*>(g_task_get_task_data(G_TASK(result)));
        notes_busy=false;notes_auth=w->reply.auth;notes_error=w->reply.error;
        if(w->reply.ok) {
            if(w->command.listing || w->command.name=="sync"){
                notes_cache=std::move(w->reply.notes);notes_loaded=true;notes_loaded_us=g_get_monotonic_time();
                if(w->command.name=="sync" && w->input==gtk_entry_get_text(GTK_ENTRY(state.entry)))gtk_entry_set_text(GTK_ENTRY(state.entry),"*read");
            } else {
                notes_cache.clear();notes_loaded=false;
                if(w->command.save)open_popup({"NOTE SYNCED","Saved to notes.aridlin.pl.\n\n"+w->command.text});
                if(w->command.name=="login")open_popup({"NOTES","Signed in to notes.aridlin.pl."});
                if(w->command.name=="logout")open_popup({"NOTES","Signed out of notes.aridlin.pl."});
                if(w->command.save && w->input==gtk_entry_get_text(GTK_ENTRY(state.entry)))
                    gtk_entry_set_text(GTK_ENTRY(state.entry),"*read");
            }
        }
        if(std::string(gtk_entry_get_text(GTK_ENTRY(state.entry))).starts_with('*'))notes_results();
    },nullptr);
    g_task_set_task_data(task,work,[](gpointer p){delete static_cast<NotesWork*>(p);});
    g_task_run_in_thread(task,[](GTask* t,gpointer,gpointer data,GCancellable*){
        auto w=static_cast<NotesWork*>(data);w->reply=notes_client.run(w->command,w->username,w->password);
        std::fill(w->password.begin(),w->password.end(),'\0');w->password.clear();g_task_return_boolean(t,TRUE);
    });
    g_object_unref(task);
}
Result notes_row(std::string title,std::string subtitle,std::string action,std::string id={}) {
    Result r;r.text=std::move(title);r.subtext=std::move(subtitle);r.action=std::move(action);
    r.identifier=std::move(id);r.provider="kalwer-notes";r.icon="accessories-text-editor";return r;
}
void notes_results() {
    const auto input=std::string(gtk_entry_get_text(GTK_ENTRY(state.entry)));
    const auto c=kalwer::notes::parse(input);
    if(c.name.empty())return;
    std::vector<Result> rows;
    if(notes_busy)rows.push_back(notes_row("SYNCING NOTES…","notes.aridlin.pl","info"));
    else if(c.name=="login")rows.push_back(notes_row("Sign in to Notes","Connect your notes.aridlin.pl account","login"));
    else if(c.name=="web")rows.push_back(notes_row("Open Notes","https://notes.aridlin.pl","web"));
    else if(c.name=="help") {
        rows.push_back(notes_row("*read","Browse and search synced notes","replace","*read"));
        rows.push_back(notes_row("*note text","Save a note · or just *text","replace","*note "));
        rows.push_back(notes_row("*someday text","Save something for someday","replace","*someday "));
        rows.push_back(notes_row("*task / *idea / *soon","Tasks, projects and soon notes","help"));
        rows.push_back(notes_row("Notes help and account","All * commands and sign-in","help"));
    }
    else if(!notes_error.empty()) {
        rows.push_back(notes_row(notes_error,"Enter to retry · your input is preserved","retry"));
        rows.push_back(notes_row("Sign in to Notes","notes.aridlin.pl account","login"));
    } else if(notes_auth)rows.push_back(notes_row("Sign in to Notes","Enter to connect your notes.aridlin.pl account","login"));
    else if(c.listing) {
        if(!notes_loaded || g_get_monotonic_time()-notes_loaded_us>15*G_USEC_PER_SEC){
            notes_start(c);rows.push_back(notes_row("LOADING NOTES…","notes.aridlin.pl","info"));
        } else {
            for(const auto& note:notes_cache) {
                if(note.status=="archived" || note.status=="abandoned")continue;
                if(c.name=="task" && note.type!="checklist")continue;
                if(c.name=="idea" && note.type!="project")continue;
                if(c.name=="note" && note.type!="markdown")continue;
                if((c.name=="someday" || c.name=="soon" || c.name=="reference" || c.name=="disposable") && note.lifecycle!=c.name)continue;
                if(!c.text.empty()) {
                    gchar* hay=g_utf8_casefold((note.title+"\n"+note.body).c_str(),-1);
                    gchar* needle=g_utf8_casefold(c.text.c_str(),-1);
                    bool match=std::string(hay).find(needle)!=std::string::npos;g_free(hay);g_free(needle);if(!match)continue;
                }
                rows.push_back(notes_row(note.title,note.type+" · "+note.lifecycle+" · "+note.status,"read",note.id));
            }
            if(rows.empty())rows.push_back(notes_row("No matching notes","*note text saves a note · *sync refreshes","info"));
        }
    } else if(c.save)rows.push_back(notes_row("SAVE "+c.name+" NOTE",c.text,"save"));
    else if(c.name=="logout")rows.push_back(notes_row("Sign out of Notes","Remove Kalwer's saved server session","logout"));
    else if(c.name=="sync")rows.push_back(notes_row("Refresh notes","Fetch the latest notes from notes.aridlin.pl","sync"));
    show_local_results(std::move(rows));
}
void notes_login() {
    if(notes_login_window){gtk_window_present(GTK_WINDOW(notes_login_window));return;}
    auto dialog=gtk_dialog_new_with_buttons("Sign in to notes.aridlin.pl",GTK_WINDOW(state.window),GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Sign in",GTK_RESPONSE_ACCEPT,nullptr);
    notes_login_window=dialog;
    auto box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box),16);
    auto username=gtk_entry_new();auto password=gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(username),"Username");
    gtk_entry_set_placeholder_text(GTK_ENTRY(password),"Password");gtk_entry_set_visibility(GTK_ENTRY(password),FALSE);
    gtk_entry_set_activates_default(GTK_ENTRY(password),TRUE);
    gtk_box_pack_start(GTK_BOX(box),username,FALSE,FALSE,4);gtk_box_pack_start(GTK_BOX(box),password,FALSE,FALSE,4);
    gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Uses your notes account. Password is not saved."),FALSE,FALSE,4);
    g_object_set_data(G_OBJECT(dialog),"username",username);g_object_set_data(G_OBJECT(dialog),"password",password);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);
    g_signal_connect(dialog,"response",G_CALLBACK(+[](GtkDialog* d,gint response,gpointer){
        if(response==GTK_RESPONSE_ACCEPT) {
            auto u=GTK_ENTRY(g_object_get_data(G_OBJECT(d),"username"));auto p=GTK_ENTRY(g_object_get_data(G_OBJECT(d),"password"));
            notes_start({"login",""},gtk_entry_get_text(u),gtk_entry_get_text(p));gtk_entry_set_text(p,"");
        }
        gtk_widget_destroy(GTK_WIDGET(d));
        notes_results();
    }),nullptr);
    g_signal_connect(dialog,"destroy",G_CALLBACK(+[](GtkWidget*,gpointer){notes_login_window=nullptr;}),nullptr);
    gtk_widget_show_all(dialog);gtk_widget_grab_focus(username);
}
void notes_activate(const Result& r) {
    if(notes_busy)return;
    if(r.action=="login")notes_login();
    else if(r.action=="web") {GError* e=nullptr;g_app_info_launch_default_for_uri("https://notes.aridlin.pl",nullptr,&e);g_clear_error(&e);}
    else if(r.action=="replace")gtk_entry_set_text(GTK_ENTRY(state.entry),r.identifier.c_str());
    else if(r.action=="help")open_popup({"NOTES HELP",kalwer::notes::help});
    else if(r.action=="read") {
        for(const auto& n:notes_cache)if(n.id==r.identifier){open_popup({n.title,n.type+" · "+n.lifecycle+" · "+n.status+"\n\n"+n.body});break;}
    } else if(r.action!="info") {
        auto c=kalwer::notes::parse(gtk_entry_get_text(GTK_ENTRY(state.entry)));
        if(r.action=="retry") {notes_error.clear();if(c.name=="login"){notes_login();return;}}
        notes_start(c);notes_results();
    }
}
