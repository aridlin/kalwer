#include "../media_preview_gtk.hpp"
#include <cassert>
#include <iostream>
#include <string>
void pump(int ms) {
  auto end = g_get_monotonic_time() + ms * 1000;
  while (g_get_monotonic_time() < end) {
    while (gtk_events_pending())
      gtk_main_iteration();
    g_usleep(10000);
  }
}
int main(int argc, char **argv) {
  gtk_init(&argc, &argv);
  auto window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  auto view = kalwer::media::create(argv[1]);
  assert(view);
  gtk_container_add(GTK_CONTAINER(window), view);
  gtk_widget_show_all(window);
  auto player = static_cast<kalwer::media::Player *>(
      g_object_get_data(G_OBJECT(view), "kalwer-media"));
  for (int i = 0; i < 100 && !player->preroll; i++)
    pump(50);
  assert(player->preroll);
  assert(player->pixels.size() == 480 * 270 * 4);
  gtk_button_clicked(GTK_BUTTON(player->button));
  assert(player->playing);
  pump(200);
  kalwer::media::pause_under(view);
  assert(!player->playing && player->timer == 0);
  gtk_widget_destroy(window);
  pump(100);
  std::cout
      << "Video preroll, bounded frame, playback, pause and cleanup passed.\n";
}
