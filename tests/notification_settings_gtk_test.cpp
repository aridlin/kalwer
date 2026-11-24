// Run in a fresh XDG_CONFIG_HOME under a private graphical session.
#define main kalwer_launcher_main
#include "../main.cpp"
#undef main
#include <cassert>
int main(int argc,char** argv) {
    gtk_init(&argc,&argv);
    state.app=gtk_application_new("pl.aridlin.KalwerSettingsTest",G_APPLICATION_NON_UNIQUE);
    assert(g_application_register(G_APPLICATION(state.app),nullptr,nullptr));
    load_settings();
    assert(!state.hide_on_blur && !state.notice_wide && state.notice_outline==kBarOutline);
    assert(state.notice_icon=="timer");
    state.resident=true;
    show_settings_window();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.settings_blur),TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.settings_notice_wide),TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_outline),4);
    gtk_entry_set_text(GTK_ENTRY(state.settings_notice_icon),"");
    save_settings_window(nullptr,nullptr);
    state.hide_on_blur=false; state.notice_wide=false; state.notice_outline=2; state.notice_icon="timer";
    load_settings();
    assert(state.hide_on_blur && state.notice_wide && state.notice_outline==4 && state.notice_icon.empty());
    g_object_unref(state.app);
}
