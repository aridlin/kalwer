#include "../file_browser_gtk.hpp"
#include "../launcher_commands.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace kalwer::files;
void settle() {
  auto until = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
  do {
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
    g_usleep(1000);
  } while (g_get_monotonic_time() < until);
}
void capture(const char *suffix) {
  if (auto output = g_getenv("SCREENSHOT")) {
    auto pix =
        gdk_pixbuf_get_from_window(gtk_widget_get_window(window), 0, 0,
                                   gtk_widget_get_allocated_width(window),
                                   gtk_widget_get_allocated_height(window));
    assert(pix);
    auto filename = std::string(output) + suffix;
    gdk_pixbuf_save(pix, filename.c_str(), "png", nullptr, nullptr);
    g_object_unref(pix);
  }
}
void open(GtkApplication *app, const std::string &path) {
  auto host = gtk_application_window_new(app);
  gtk_window_set_default_size(GTK_WINDOW(host), 452, 397);
  auto root =
      kalwer::files::create(host, path, [] { gtk_widget_destroy(window); });
  gtk_container_add(GTK_CONTAINER(host), root);
  gtk_widget_show_all(host);
  g_signal_connect(
      host, "key-press-event",
      G_CALLBACK(+[](GtkWidget *, GdkEventKey *e, gpointer) -> gboolean {
        return kalwer::files::key(e);
      }),
      nullptr);
}
void key(guint keyval,
         GdkModifierType modifiers = static_cast<GdkModifierType>(0)) {
  GdkEventKey e{};
  e.type = GDK_KEY_PRESS;
  e.keyval = keyval;
  e.state = modifiers;
  gboolean handled = FALSE;
  g_signal_emit_by_name(window, "key-press-event", &e, &handled);
  assert(handled);
}
int main(int argc, char **argv) {
  gtk_init(&argc, &argv);
  auto app = gtk_application_new("pl.aridlin.kalwer.files.test",
                                 G_APPLICATION_NON_UNIQUE);
  assert(g_application_register(G_APPLICATION(app), nullptr, nullptr));
  auto root = std::string(g_getenv("TEST_ROOT"));
  g_mkdir_with_parents((root + "/Folder").c_str(), 0700);
  std::ofstream(root + "/file with spaces #żółw.txt") << "example";
  auto pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 160, 90);
  gdk_pixbuf_fill(pix, 0x4188bbff);
  assert(gdk_pixbuf_save(pix, (root + "/Picture.png").c_str(), "png", nullptr,
                         nullptr));
  g_object_unref(pix);
  auto set_date = [](const std::string &path, guint64 seconds) {
    auto file = g_file_new_for_path(path.c_str());
    assert(g_file_set_attribute_uint64(file, G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                       seconds, G_FILE_QUERY_INFO_NONE, nullptr,
                                       nullptr));
    g_object_unref(file);
  };
  set_date(root + "/Picture.png", 1700000000);
  set_date(root + "/file with spaces #żółw.txt", 1700003600);
  assert(expand("~/Downloads") == std::string(g_get_home_dir()) + "/Downloads");
  assert(expand("\"/tmp/a b\"") == "/tmp/a b");
  assert(kalwer::matching_commands("/file /tmp").size() == 1);
  assert(kalwer::matching_commands("/files").size() == 1);
  assert(!folders_first(expand("")) && folders_first(root));
  std::vector<Entry> ordering{
      {"/old", "old", "folder", "", true, 1, "old"},
      {"/new", "new", "text-x-generic", "", false, 2, "new"}};
  sort_entries(ordering, false);
  assert(ordering.front().name == "new");
  sort_entries(ordering, true);
  assert(ordering.front().name == "old");
  open(app, root);
  settle();
  assert(directory == root);
  assert(gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(grid), 2));
  auto child = gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(grid), 0);
  assert(std::string(static_cast<char *>(
             g_object_get_data(G_OBJECT(child), "name"))) == "Folder");
  auto newest = gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(grid), 1);
  assert(std::string(static_cast<const char *>(g_object_get_data(
             G_OBJECT(newest), "name"))) == "file with spaces #żółw.txt");
  auto oldest = gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(grid), 2);
  assert(std::string(static_cast<const char *>(
             g_object_get_data(G_OBJECT(oldest), "name"))) == "Picture.png");
  gtk_entry_set_text(GTK_ENTRY(search), "PICTURE");
  settle();
  assert(matches.size() == 1);
  assert(std::string(static_cast<const char *>(g_object_get_data(
             G_OBJECT(gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(grid), 0)),
             "name"))) == "Picture.png");
  gtk_entry_set_text(GTK_ENTRY(search), "");
  settle();
  GtkFlowBoxChild *picture = nullptr;
  for (int i = 0; i < 3; ++i) {
    auto c = gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(grid), i);
    if (std::string(static_cast<char *>(
            g_object_get_data(G_OBJECT(c), "name"))) == "Picture.png")
      picture = c;
  }
  assert(picture);
  gtk_flow_box_select_child(GTK_FLOW_BOX(grid), picture);
  gtk_widget_grab_focus(GTK_WIDGET(picture));
  key(GDK_KEY_c, GDK_CONTROL_MASK);
  auto selected_data = gtk_clipboard_wait_for_contents(
      gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
      gdk_atom_intern_static_string("text/uri-list"));
  assert(selected_data);
  auto selected_uris = gtk_selection_data_get_uris(selected_data);
  assert(selected_uris &&
         std::string(selected_uris[0]) == uri(root + "/Picture.png"));
  g_strfreev(selected_uris);
  gtk_selection_data_free(selected_data);
  auto tile = gtk_bin_get_child(GTK_BIN(picture));
  auto box = gtk_bin_get_child(GTK_BIN(tile));
  auto kids = gtk_container_get_children(GTK_CONTAINER(box));
  assert(gtk_image_get_storage_type(GTK_IMAGE(kids->data)) == GTK_IMAGE_PIXBUF);
  g_list_free(kids);
  // Drive a real pointer drag into a separate GTK drop target. This catches
  // event consumption that accidentally prevents native file DND.
  gboolean window_dragging = TRUE;
  gtk_widget_style_get(GTK_WIDGET(picture), "window-dragging", &window_dragging,
                       nullptr);
  assert(!window_dragging);
  auto sink = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size(GTK_WINDOW(sink), 200, 180);
  gtk_window_move(GTK_WINDOW(sink), 800, 100);
  gtk_drag_dest_set(sink, GTK_DEST_DEFAULT_ALL, targets, 1, GDK_ACTION_COPY);
  bool received = false;
  g_signal_connect(
      sink, "drag-data-received",
      G_CALLBACK(+[](GtkWidget *, GdkDragContext *context, gint, gint,
                     GtkSelectionData *data, guint, guint time, gpointer user) {
        auto uris = gtk_selection_data_get_uris(data);
        *static_cast<bool *>(user) =
            uris && std::string(uris[0]).ends_with("/Picture.png");
        g_strfreev(uris);
        gtk_drag_finish(context, TRUE, FALSE, time);
      }),
      &received);
  gtk_widget_show_all(sink);
  settle();
  int ox = 0, oy = 0, x = 0, y = 0;
  gdk_window_get_origin(gtk_widget_get_window(window), &ox, &oy);
  gtk_widget_translate_coordinates(GTK_WIDGET(picture), window, 30, 30, &x, &y);
  auto script = g_strdup_printf(
      "xdotool mousemove --sync %d %d mousedown 1 sleep 0.15 mousemove --sync "
      "%d %d sleep 0.15 mousemove --sync 880 160 sleep 0.4 mouseup 1",
      ox + x, oy + y, ox + x + 35, oy + y + 5);
  gchar *args[] = {const_cast<gchar *>("sh"), const_cast<gchar *>("-c"), script,
                   nullptr};
  assert(g_spawn_async(nullptr, args, nullptr, G_SPAWN_SEARCH_PATH, nullptr,
                       nullptr, nullptr, nullptr));
  g_free(script);
  settle();
  assert(received);
  int end_x = 0, end_y = 0;
  gdk_window_get_origin(gtk_widget_get_window(window), &end_x, &end_y);
  assert(end_x == ox && end_y == oy);
  gtk_widget_style_get(window, "window-dragging", &window_dragging, nullptr);
  assert(!window_dragging);
  gtk_widget_destroy(sink);
  capture("-browser.png");
  gtk_button_clicked(GTK_BUTTON(bookmark));
  assert(bookmarks.size() == 1);
  assert(g_file_test(config().c_str(), G_FILE_TEST_EXISTS));
  show_file(root + "/file with spaces #żółw.txt");
  assert(previewing);
  settle();
  auto preview_children =
      gtk_container_get_children(GTK_CONTAINER(preview_box));
  bool text_found = false;
  for (auto p = preview_children; p; p = p->next)
    if (GTK_IS_SCROLLED_WINDOW(p->data)) {
      auto view = gtk_bin_get_child(GTK_BIN(p->data));
      auto buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
      GtkTextIter start, end;
      gtk_text_buffer_get_bounds(buffer, &start, &end);
      auto text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
      assert(std::string(text) == "example");
      g_free(text);
      text_found = true;
    }
  g_list_free(preview_children);
  assert(text_found);
  copy();
  auto clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  auto data = gtk_clipboard_wait_for_contents(
      clipboard, gdk_atom_intern_static_string("text/uri-list"));
  assert(data);
  auto uris = gtk_selection_data_get_uris(data);
  assert(uris && std::string(uris[0]) == uri(selected));
  g_strfreev(uris);
  gtk_selection_data_free(data);
  data = gtk_clipboard_wait_for_contents(
      clipboard, gdk_atom_intern_static_string("x-special/gnome-copied-files"));
  assert(data);
  assert(std::string(
             reinterpret_cast<const char *>(gtk_selection_data_get_data(data)),
             gtk_selection_data_get_length(data))
             .starts_with("copy\nfile:///"));
  gtk_selection_data_free(data);
  key(GDK_KEY_Escape);
  assert(!previewing);
  show_file(root + "/Picture.png");
  settle();
  if (auto output = g_getenv("SCREENSHOT")) {
    auto pix =
        gdk_pixbuf_get_from_window(gtk_widget_get_window(window), 0, 0,
                                   gtk_widget_get_allocated_width(window),
                                   gtk_widget_get_allocated_height(window));
    assert(pix);
    gdk_pixbuf_save(pix, output, "png", nullptr, nullptr);
    g_object_unref(pix);
  }
  capture("-preview.png");
  kalwer::files::back();
  assert(!previewing);
  browse(root + "/missing");
  settle();
  assert(directory == root);
  assert(std::string(gtk_label_get_text(GTK_LABEL(status))).size() > 0);
  browse(root + "/Folder");
  browse(root);
  settle();
  assert(directory == root);
  gtk_widget_destroy(window);
  assert(!window);
  open(app, root + "/Picture.png");
  settle();
  assert(previewing && selected == root + "/Picture.png");
  assert(bookmarks.size() == 1);
  kalwer::files::back();
  settle();
  auto saved_entries = entries;
  entries.clear();
  for (int i = 0; i < 5000; ++i) {
    auto name = "bulk-" + std::to_string(i) + ".png";
    entries.push_back({root + "/" + name, name, "image-x-generic", "", false,
                       static_cast<guint64>(i), name});
  }
  auto started = image_started;
  auto start = g_get_monotonic_time();
  refill();
  auto render_us = g_get_monotonic_time() - start;
  settle();
  assert(entries.size() == 5000 && matches.size() == 5000 &&
         rendered == page_size);
  const auto initial_image_jobs = image_started - started;
  assert(image_peak <= 2 && initial_image_jobs < 20);
  auto adjustment =
      gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller));
  gtk_adjustment_set_value(adjustment,
                           gtk_adjustment_get_upper(adjustment) -
                               gtk_adjustment_get_page_size(adjustment));
  settle();
  assert(rendered > page_size && rendered < 5000);
  gtk_entry_set_text(GTK_ENTRY(search), "bulk-4999");
  settle();
  assert(matches.size() == 1 && rendered == 1);
  assert(entries[matches.front()].name == "bulk-4999.png");
  std::cout << "5000-entry grid: initial tiles=" << page_size
            << ", initial image jobs=" << initial_image_jobs
            << ", max parallel=" << image_peak
            << ", initial render ms=" << render_us / 1000.0
            << "; full-list search and incremental scrolling passed\n";
  entries = std::move(saved_entries);
  gtk_widget_destroy(window);
  open(app, root);
  gtk_widget_destroy(window);
  settle();
  assert(!window);
  g_object_unref(app);
  std::cout << "Browser navigation, thumbnail, filter, bookmarks, preview, "
               "Escape/X, clipboard URI formats, stale navigation and "
               "close-during-load, real pointer file drag and newest-first "
               "date sorting passed\n";
}
