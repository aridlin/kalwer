#include "../popup_placement.hpp"
#include <cassert>
#include <iostream>
void settle() {
  auto until = g_get_monotonic_time() + 300000;
  do {
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
    g_usleep(1000);
  } while (g_get_monotonic_time() < until);
}
int main(int argc, char **argv) {
  gtk_init(&argc, &argv);
  auto launcher = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
  gtk_window_set_default_size(launcher, 650, 632);
  gtk_window_move(launcher, 20, 50);
  gtk_widget_show_all(GTK_WIDGET(launcher));
  settle();
  auto popup = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
  gtk_window_set_default_size(popup, 480, 430);
  gtk_window_set_decorated(popup, FALSE);
  kalwer::place_output_popup(popup, launcher, 635, 27, true);
  gtk_widget_show_all(GTK_WIDGET(popup));
  settle();
  auto p = static_cast<kalwer::PopupPlacement *>(
      g_object_get_data(G_OBJECT(popup), "kalwer-popup-placement"));
  assert(p && p->right_edge);
  GdkRectangle area{};
  gdk_monitor_get_workarea(p->monitor, &area);
  int width = 0, height = 0;
  gtk_window_get_size(popup, &width, &height);
  auto expected = kalwer::popup_geometry(area, width, height);
  for (double progress : {0., 0.5, 1.}) {
    kalwer::animate_popup_position(popup, progress);
    settle();
    int x = 0, y = 0;
    gtk_window_get_position(popup, &x, &y);
    assert(x == expected.x && y == expected.y);
  }
  gtk_widget_destroy(GTK_WIDGET(popup));
  gtk_widget_destroy(GTK_WIDGET(launcher));
  std::cout << "File popup stays at monitor right edge throughout the reveal; "
               "standard popup geometry remains separate\n";
}
