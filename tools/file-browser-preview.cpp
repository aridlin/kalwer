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
        auto command = g_getenv("KALWER_REVIEW_COMMAND");
        gtk_entry_set_text(GTK_ENTRY(state.entry),
                           command ? command :
                           path ? (std::string("/file ") + path).c_str()
                                : "/file");
        activate_selection();
        if (g_getenv("KALWER_REVIEW_CAPTURE"))
          g_timeout_add(
              3000,
              +[](gpointer) -> gboolean {
                g_assert(state.output_files && !state.output_terminal &&
                         kalwer::files::window == state.output_window);
                std::fprintf(
                    stderr,
                    "File browser: entries=%zu, tiles=%zu, thumbnail jobs=%u, "
                    "max parallel=%u\n",
                    kalwer::files::entries.size(), kalwer::files::rendered,
                    kalwer::files::image_started, kalwer::files::image_peak);
                auto placement = static_cast<kalwer::PopupPlacement *>(
                    g_object_get_data(G_OBJECT(state.output_window),
                                      "kalwer-popup-placement"));
                g_assert(placement && placement->right_edge);
                int x = 0, y = 0, w = 0, h = 0;
                gtk_window_get_position(GTK_WINDOW(state.output_window), &x,
                                        &y);
                gtk_window_get_size(GTK_WINDOW(state.output_window), &w, &h);
                GdkRectangle area{};
                gdk_monitor_get_workarea(placement->monitor, &area);
                auto expected = kalwer::popup_geometry(area, w, h);
                g_assert(x == expected.x && y == expected.y);
                {
                  std::lock_guard lock(kalwer::live_backdrop.mutex);
                  g_assert(!kalwer::live_backdrop.active);
                }
                auto surface =
                    cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
                auto cr = cairo_create(surface);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_set_source_rgba(cr, 0, 0, 0, 0);
                cairo_paint(cr);
                cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                gtk_widget_draw(state.output_window, cr);
                cairo_destroy(cr);
                cairo_surface_flush(surface);
                auto pixels = reinterpret_cast<const unsigned *>(
                    cairo_image_surface_get_data(surface));
                int stride = cairo_image_surface_get_stride(surface) / 4;
                unsigned transparent = 0, total = 0;
                for (int yy = h - 90; yy < h - 40; ++yy)
                  for (int xx = 30; xx < w - 20; ++xx) {
                    ++total;
                    if ((pixels[yy * stride + xx] >> 24) < 250)
                      ++transparent;
                  }
                g_assert(transparent > total / 10);
                std::fprintf(stderr,
                             "Native transparency: %u/%u sampled background "
                             "pixels remain translucent\n",
                             transparent, total);
                cairo_surface_write_to_png(surface,
                                           g_getenv("KALWER_REVIEW_CAPTURE"));
                cairo_surface_destroy(surface);
                auto pix = gdk_pixbuf_get_from_window(
                    gtk_widget_get_window(state.output_window), 0, 0,
                    gtk_widget_get_allocated_width(state.output_window),
                    gtk_widget_get_allocated_height(state.output_window));
                if (pix) {
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
