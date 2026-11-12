// Review the real Kalwer popup, including its frame, reveal and placement.
#define main kalwer_application_main
#include "../main.cpp"
#undef main
int main(int argc, char **argv) {
  load_favorites();
  load_settings();
  auto app = gtk_application_new("pl.aridlin.KalwerFileReview",
                                 G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(
      app, "activate", G_CALLBACK(+[](GtkApplication *app, gpointer) {
        activate(app, nullptr);
        auto path = g_getenv("KALWER_REVIEW_PATH");
        gtk_entry_set_text(GTK_ENTRY(state.entry),
                           path ? (std::string("/file ") + path).c_str()
                                : "/file");
        activate_selection();
        if (g_getenv("KALWER_REVIEW_CAPTURE"))
          g_timeout_add(
              3000,
              +[](gpointer) -> gboolean {
                g_assert(state.output_files && !state.output_terminal &&
                         kalwer::files::window == state.output_window);
                auto pix = gdk_pixbuf_get_from_window(
                    gtk_widget_get_window(state.output_window), 0, 0,
                    gtk_widget_get_allocated_width(state.output_window),
                    gtk_widget_get_allocated_height(state.output_window));
                if (pix) {
                  gdk_pixbuf_save(pix, g_getenv("KALWER_REVIEW_CAPTURE"), "png",
                                  nullptr, nullptr);
                  g_object_unref(pix);
                }
                gtk_widget_destroy(state.output_window);
                return G_SOURCE_REMOVE;
              },
              nullptr);
        if (state.output_window)
          g_signal_connect(state.output_window, "destroy",
                           G_CALLBACK(+[](GtkWidget *, gpointer) {
                             g_application_quit(G_APPLICATION(state.app));
                           }),
                           nullptr);
      }),
      nullptr);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  cleanup();
  g_object_unref(app);
  return status;
}
