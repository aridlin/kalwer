#pragma once
#include "fred/api.h"
#include <algorithm>
#include <array>
#include <dlfcn.h>
#include <functional>
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <string>

namespace kalwer::fred_popup {
constexpr int width = 550, height = 486;
struct Canvas {
  void *library = nullptr;
  const KalwerFredApi *api = nullptr;
  void *game = nullptr;
  guint timer = 0;
  uint32_t held = 0, pressed = 0;
  gint64 last = 0;
  bool focused = false;
  std::string error;
  std::function<std::array<double, 3>()> animation;
  std::function<void()> close;
  ~Canvas() {
    if (timer)
      g_source_remove(timer);
    if (game)
      api->destroy(game);
    if (library)
      dlclose(library);
  }
};
inline Canvas *session(GtkWidget *w) {
  return static_cast<Canvas *>(g_object_get_data(G_OBJECT(w), "kalwer-fred"));
}
inline void focus(GtkWidget *w, bool active) {
  auto *s = session(w);
  s->focused = active;
  s->held = s->pressed = 0;
  s->last = g_get_monotonic_time();
}
inline void key(GtkWidget *w, guint keyval, bool down) {
  auto *s = session(w);
  uint32_t b = 0;
  switch (gdk_keyval_to_lower(keyval)) {
  case GDK_KEY_Left:
  case GDK_KEY_q:
    b = FredLeft;
    break;
  case GDK_KEY_Right:
  case GDK_KEY_w:
    b = FredRight;
    break;
  case GDK_KEY_Up:
  case GDK_KEY_r:
    b = FredUp;
    break;
  case GDK_KEY_Down:
  case GDK_KEY_e:
    b = FredDown;
    break;
  case GDK_KEY_space:
  case GDK_KEY_t:
    b = FredFire;
    break;
  case GDK_KEY_Return:
  case GDK_KEY_KP_Enter:
    b = FredStart;
    break;
  }
  if (down) {
    if (!(s->held & b))
      s->pressed |= b;
    s->held |= b;
  } else
    s->held &= ~b;
}
inline GtkWidget *create(GtkWidget *window,
                         std::function<std::array<double, 3>()> animation,
                         std::function<void()> close) {
  auto *s = new Canvas;
  s->animation = std::move(animation);
  s->close = std::move(close);
  std::string path =
      std::string(g_get_user_data_dir()) + "/kalwer/fred/libkalwer-fred.so";
  s->library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (s->library) {
    auto entry = reinterpret_cast<const KalwerFredApi *(*)()>(
        dlsym(s->library, "kalwer_fred_api"));
    if (entry)
      s->api = entry();
    if (s->api && s->api->version == 1 && s->api->create && s->api->destroy &&
        s->api->step && s->api->pixels)
      s->game = s->api->create();
  }
  if (!s->game)
    s->error = "Fred is not installed. See fred/README.md.\nBuild it using "
               "your own original reference file.";
  auto *w = gtk_drawing_area_new();
  gtk_widget_set_can_focus(w, TRUE);
  g_object_set_data_full(
      G_OBJECT(w), "kalwer-fred", s,
      +[](gpointer p) { delete static_cast<Canvas *>(p); });
  gtk_widget_add_events(w, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(
      w, "draw",
      G_CALLBACK(+[](GtkWidget *w, cairo_t *cr, gpointer) -> gboolean {
        auto *s = session(w);
        auto a = s->animation();
        const double left = 18 * a[1];
        const double bottom = 18 + (height - 26) * a[2];
        cairo_scale(cr, gtk_widget_get_allocated_width(w) / double(width),
                    gtk_widget_get_allocated_height(w) / double(height));
        cairo_save(cr);
        cairo_rectangle(cr, left, 18, width - 8 - left, bottom - 18);
        cairo_clip(cr);
        cairo_translate(cr, 0, 18);
        cairo_scale(cr, 1, std::max(.0001, a[2]));
        cairo_translate(cr, 0, -18);
        cairo_set_source_rgba(cr, .025, .07, .06, .98);
        cairo_paint(cr);
        cairo_set_source_rgb(cr, .88, .96, .91);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12);
        cairo_move_to(cr, 26, 40);
        cairo_show_text(cr, "FRED / NATIVE");
        cairo_move_to(cr, width - 33, 40);
        cairo_show_text(cr, "x");
        if (s->game) {
          auto *image = cairo_image_surface_create_for_data(
              reinterpret_cast<unsigned char *>(
                  const_cast<uint32_t *>(s->api->pixels(s->game))),
              CAIRO_FORMAT_RGB24, 256, 192, 256 * 4);
          cairo_save(cr);
          cairo_translate(cr, 24, 50);
          cairo_scale(cr, 2, 2);
          cairo_set_source_surface(cr, image, 0, 0);
          cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
          cairo_paint(cr);
          cairo_restore(cr);
          cairo_surface_destroy(image);
        }
        cairo_set_source_rgb(cr, .88, .96, .91);
        cairo_set_font_size(cr, 10);
        cairo_move_to(cr, 25, 450);
        cairo_show_text(cr,
                        "Enter: start   Arrows / QWER: move   Space / T: fire");
        if (!s->error.empty()) {
          auto *layout = gtk_widget_create_pango_layout(w, s->error.c_str());
          pango_layout_set_width(layout, 470 * PANGO_SCALE);
          cairo_move_to(cr, 30, 90);
          pango_cairo_show_layout(cr, layout);
          g_object_unref(layout);
        }
        cairo_restore(cr);
        cairo_set_source_rgb(cr, .55, .91, .70);
        cairo_set_line_width(cr, 1.5);
        if (a[2] > 0) {
          cairo_move_to(cr, left, 18);
          cairo_line_to(cr, left, bottom);
          cairo_line_to(cr, width - 8, bottom);
          cairo_line_to(cr, width - 8, 18);
          cairo_stroke(cr);
        }
        cairo_move_to(cr, left, 18);
        cairo_line_to(cr, left + (width - 26) * a[0], 18);
        cairo_stroke(cr);
        return TRUE;
      }),
      nullptr);
  g_signal_connect_object(
      window, "notify::is-active",
      G_CALLBACK(+[](GObject *win, GParamSpec *, gpointer p) {
        focus(GTK_WIDGET(p), gtk_window_is_active(GTK_WINDOW(win)));
      }),
      w, G_CONNECT_DEFAULT);
  g_signal_connect(
      w, "button-press-event",
      G_CALLBACK(+[](GtkWidget *w, GdkEventButton *e, gpointer) -> gboolean {
        auto *s = session(w);
        if (s->animation()[2] < .999)
          return TRUE;
        double x = e->x * width / gtk_widget_get_allocated_width(w);
        double y = e->y * height / gtk_widget_get_allocated_height(w);
        if (x > width - 45 && y > 20 && y < 48)
          s->close();
        else
          gtk_widget_grab_focus(w);
        return TRUE;
      }),
      nullptr);
  s->timer = g_timeout_add(
      16,
      +[](gpointer p) -> gboolean {
        auto *w = GTK_WIDGET(p);
        auto *s = session(w);
        auto now = g_get_monotonic_time();
        if (!s->focused || s->animation()[2] < .999) {
          s->last = now;
          return G_SOURCE_CONTINUE;
        }
        if (s->game && s->error.empty() && now - s->last >= 180000) {
          // Match the upstream native app's 180 ms logical tick; preserve short
          // taps.
          uint32_t buttons = (s->held & ~FredStart) | s->pressed;
          s->pressed = 0;
          s->last = now;
          if (!s->api->step(s->game, buttons))
            s->error = "Fred's native core could not complete this frame.";
          gtk_widget_queue_draw(w);
        }
        return G_SOURCE_CONTINUE;
      },
      w);
  return w;
}
} // namespace kalwer::fred_popup
