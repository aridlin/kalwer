#pragma once
#include "categories.hpp"
#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>
namespace kalwer::categories {
inline std::string config_path() { return std::string(g_get_user_config_dir())+"/kalwer/categories-v1.ini"; }
inline std::vector<App> installed_apps() {
    std::vector<App> apps;auto* all=g_app_info_get_all();
    for(auto* i=all;i;i=i->next) {
        auto* info=G_APP_INFO(i->data);if(!g_app_info_should_show(info)||!g_app_info_get_id(info))continue;
        const char* tags=G_IS_DESKTOP_APP_INFO(info)?g_desktop_app_info_get_categories(G_DESKTOP_APP_INFO(info)):nullptr;
        auto* icon=g_app_info_get_icon(info);auto* icon_name=icon?g_icon_to_string(icon):nullptr;
        apps.push_back({g_app_info_get_id(info),g_app_info_get_display_name(info),tags?tags:"",icon_name?icon_name:"application-x-executable"});g_free(icon_name);
    }
    g_list_free_full(all,g_object_unref);
    for(const auto& c:kalwer::commands)if(kalwer::games::is_command(std::string(c.name))||c.name=="/fred")apps.push_back({std::string(c.name),std::string(c.name.substr(1))+" (Kalwer)","Game;","applications-games"});
    return apps;
}
struct Editor {
    GtkWidget *dialog, *name, *automatic, *tag, *tree, *status;
    GtkListStore* store;
    std::vector<Category> cats;
    std::vector<App> apps;
};
inline void fill_editor(Editor& e,const Category& c) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(e.automatic),c.automatic);
    gtk_entry_set_text(GTK_ENTRY(e.tag),c.tag.c_str());gtk_list_store_clear(e.store);
    auto selected=members(c,e.apps);
    auto add=[&](const App& a,bool yes){GtkTreeIter it;gtk_list_store_append(e.store,&it);gtk_list_store_set(e.store,&it,0,yes,1,a.name.c_str(),2,a.id.c_str(),-1);};
    for(const auto& a:selected)add(a,true);
    auto rest=e.apps;std::stable_sort(rest.begin(),rest.end(),[](const auto& a,const auto& b){return lower(a.name)<lower(b.name);});
    for(const auto& a:rest)if(std::none_of(selected.begin(),selected.end(),[&](const auto& b){return a.id==b.id;}))add(a,false);
}
inline void edit(GtkWindow* parent,const std::string& selected={}) {
    Editor e{};e.cats=load(config_path());e.apps=installed_apps();
    e.dialog=gtk_dialog_new_with_buttons("Kalwer categories",parent,GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_ACCEPT,nullptr);
    gtk_window_set_default_size(GTK_WINDOW(e.dialog),560,620);
    auto* box=gtk_dialog_get_content_area(GTK_DIALOG(e.dialog));gtk_box_set_spacing(GTK_BOX(box),8);gtk_container_set_border_width(GTK_CONTAINER(box),12);
    gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Choose a category, or type a new name (letters, digits, - or _)."),FALSE,FALSE,0);
    auto* combo=gtk_combo_box_text_new_with_entry();e.name=gtk_bin_get_child(GTK_BIN(combo));
    int current=0;for(size_t i=0;i<e.cats.size();++i){gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),e.cats[i].name.c_str());if(e.cats[i].name==selected)current=static_cast<int>(i);}
    gtk_box_pack_start(GTK_BOX(box),combo,FALSE,FALSE,0);
    e.automatic=gtk_check_button_new_with_label("Automatic: include new apps matching the desktop category");gtk_box_pack_start(GTK_BOX(box),e.automatic,FALSE,FALSE,0);
    auto* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(row),gtk_label_new("Desktop category:"),FALSE,FALSE,0);e.tag=gtk_entry_new();gtk_box_pack_start(GTK_BOX(row),e.tag,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Manual: check apps and drag rows to arrange them.\nAutomatic: checked apps stay first; matching new apps follow."),FALSE,FALSE,0);
    e.store=gtk_list_store_new(3,G_TYPE_BOOLEAN,G_TYPE_STRING,G_TYPE_STRING);e.tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(e.store));gtk_tree_view_set_reorderable(GTK_TREE_VIEW(e.tree),TRUE);
    auto* toggle=gtk_cell_renderer_toggle_new();
    g_signal_connect(toggle,"toggled",G_CALLBACK((+[](GtkCellRendererToggle*,gchar* path,gpointer p){auto& ed=*static_cast<Editor*>(p);GtkTreeIter it;if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(ed.store),&it,path)){gboolean yes;gtk_tree_model_get(GTK_TREE_MODEL(ed.store),&it,0,&yes,-1);gtk_list_store_set(ed.store,&it,0,!yes,-1);}})),&e);
    gtk_tree_view_append_column(GTK_TREE_VIEW(e.tree),gtk_tree_view_column_new_with_attributes("Include",toggle,"active",0,nullptr));
    gtk_tree_view_append_column(GTK_TREE_VIEW(e.tree),gtk_tree_view_column_new_with_attributes("Application",gtk_cell_renderer_text_new(),"text",1,nullptr));
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(e.tree),1);
    auto* scroll=gtk_scrolled_window_new(nullptr,nullptr);gtk_container_add(GTK_CONTAINER(scroll),e.tree);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
    row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    for(int direction:{-1,1}) {
        auto* button=gtk_button_new_with_label(direction<0?"Move up":"Move down");g_object_set_data(G_OBJECT(button),"direction",GINT_TO_POINTER(direction));
        g_signal_connect(button,"clicked",G_CALLBACK((+[](GtkButton* b,gpointer p){auto& ed=*static_cast<Editor*>(p);GtkTreeIter it,other;GtkTreeModel* model=nullptr;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(ed.tree)),&model,&it))return;other=it;int d=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b),"direction"));if(d<0?gtk_tree_model_iter_previous(model,&other):gtk_tree_model_iter_next(model,&other))gtk_list_store_swap(ed.store,&it,&other);})),&e);
        gtk_box_pack_start(GTK_BOX(row),button,FALSE,FALSE,0);
    }
    gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);e.status=gtk_label_new("");gtk_box_pack_start(GTK_BOX(box),e.status,FALSE,FALSE,0);
    g_signal_connect(combo,"changed",G_CALLBACK((+[](GtkComboBox*,gpointer p){auto& ed=*static_cast<Editor*>(p);std::string name=lower(gtk_entry_get_text(GTK_ENTRY(ed.name)));auto it=std::find_if(ed.cats.begin(),ed.cats.end(),[&](const auto& c){return c.name==name;});fill_editor(ed,it==ed.cats.end()?Category{name,"",false,{}}:*it);})),&e);
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo),current);gtk_widget_show_all(e.dialog);
    while(gtk_dialog_run(GTK_DIALOG(e.dialog))==GTK_RESPONSE_ACCEPT) {
        std::string name=lower(gtk_entry_get_text(GTK_ENTRY(e.name)));
        if(!valid_name(name)){gtk_label_set_text(GTK_LABEL(e.status),"Use 1–48 letters, digits, - or _. 'manage' is reserved.");continue;}
        Category c{name,gtk_entry_get_text(GTK_ENTRY(e.tag)),bool(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(e.automatic))),{}};
        GtkTreeIter it;gboolean valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(e.store),&it);
        while(valid){gboolean yes;gchar* id=nullptr;gtk_tree_model_get(GTK_TREE_MODEL(e.store),&it,0,&yes,2,&id,-1);if(yes)c.order.emplace_back(id);g_free(id);valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(e.store),&it);}
        auto next=e.cats;auto found=std::find_if(next.begin(),next.end(),[&](const auto& x){return x.name==name;});if(found==next.end())next.push_back(c);else *found=c;
        GError* error=nullptr;if(save(config_path(),next,&error))break;
        gtk_label_set_text(GTK_LABEL(e.status),error?error->message:"Could not save categories");g_clear_error(&error);
    }
    gtk_widget_destroy(e.dialog);g_object_unref(e.store);
}
}
