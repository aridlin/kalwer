#include "../fred_gtk.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

int main(int argc, char **argv) {
  gtk_init(&argc, &argv);
  auto *window = gtk_offscreen_window_new();
  gtk_window_set_default_size(GTK_WINDOW(window), kalwer::fred_popup::width,
                              kalwer::fred_popup::height);
  auto *canvas = kalwer::fred_popup::create(
      window, [] { return std::array<double, 3>{1, 1, 1}; }, [] {});
  gtk_container_add(GTK_CONTAINER(window), canvas);
  gtk_widget_show_all(window);
  while (gtk_events_pending())
    gtk_main_iteration();
  auto *s = kalwer::fred_popup::session(canvas);
  assert(s->game && s->error.empty());
  auto *pixels = s->api->pixels(s->game);
  std::vector<uint32_t> menu(pixels, pixels + 256 * 192);
  kalwer::fred_popup::key(canvas, GDK_KEY_Return, true);
  kalwer::fred_popup::key(canvas, GDK_KEY_Return, false);
  assert(s->pressed == FredStart &&
         s->held == 0); // A tap survives until the simulation tick.
  assert(s->api->step(s->game, s->pressed));
  s->pressed = 0;
  assert(std::memcmp(menu.data(), s->api->pixels(s->game),
                     menu.size() * sizeof(uint32_t)) != 0);
  kalwer::fred_popup::key(canvas, GDK_KEY_Left, true);
  kalwer::fred_popup::focus(canvas, false);
  assert(!s->focused && s->held == 0 && s->pressed == 0);
  std::vector<uint32_t> paused(s->api->pixels(s->game),
                               s->api->pixels(s->game) + 256 * 192);
  auto until = g_get_monotonic_time() + 220000;
  while (g_get_monotonic_time() < until) {
    while (gtk_events_pending())
      gtk_main_iteration();
    g_usleep(1000);
  }
  assert(std::memcmp(paused.data(), s->api->pixels(s->game),
                     paused.size() * sizeof(uint32_t)) == 0);
  gtk_widget_queue_draw(canvas);
  while (gtk_events_pending())
    gtk_main_iteration();
  auto *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kalwer::fred_popup::width,
                                 kalwer::fred_popup::height);
  auto *cr = cairo_create(surface);
  gtk_widget_draw(canvas, cr);
  assert(cairo_surface_write_to_png(surface, "/tmp/kalwer-fred-preview.png") ==
         CAIRO_STATUS_SUCCESS);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  gtk_widget_destroy(window);
  std::cout << "Fred module initialization, start, short taps, pause and popup "
               "rendering passed\n";
}
