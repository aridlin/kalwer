#include "../service_popup_gtk.hpp"
#include <cassert>
#include <iostream>
void wait(const std::function<bool()> &ready) {
  auto end = g_get_monotonic_time() + 30000000;
  while (!ready() && g_get_monotonic_time() < end) {
    while (gtk_events_pending())
      gtk_main_iteration();
    g_usleep(10000);
  }
  assert(ready());
}
int main(int argc, char **argv) {
  gtk_init(&argc, &argv);
  auto host = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  auto root = kalwer::services::create(host, true, [] {});
  gtk_container_add(GTK_CONTAINER(host), root);
  gtk_widget_show_all(host);
  auto ref = static_cast<std::shared_ptr<kalwer::services::Popup> *>(
      g_object_get_data(G_OBJECT(host), "kalwer-service-popup"));
  auto p = *ref;
  wait([&] { return !p->busy; });
  assert(!kalwer::files::entries.empty());
  auto path = kalwer::files::entries.front().path;
  p->open(path, true);
  wait([&] { return !p->busy; });
  assert(g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR));
  auto clipboard =
      gtk_clipboard_wait_for_uris(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
  assert(clipboard);
  g_strfreev(clipboard);
  p->choose();
  assert(p->choosing);
  p->restore();
  wait([&] { return !p->busy; });
  assert(!p->choosing);
  assert(GTK_IS_COMBO_BOX(p->category));
  auto end = g_get_monotonic_time() + 500000;
  while (g_get_monotonic_time() < end) {
    while (gtk_events_pending())
      gtk_main_iteration();
    g_usleep(10000);
  }
  assert(kalwer::files::service_mode);
  assert(!kalwer::files::entries.empty());
  assert(kalwer::files::entries.front().path.find("service-media") !=
         std::string::npos);
  gtk_widget_destroy(host);
  while (gtk_events_pending())
    gtk_main_iteration();
  assert(!p->alive);
  std::cout << "Memes popup catalog, download/copy, upload picker return and "
               "cleanup passed.\n";
}
