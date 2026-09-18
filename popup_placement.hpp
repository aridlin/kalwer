#pragma once
#include <gtk/gtk.h>
#include <algorithm>
#include <cmath>
#ifdef KALWER_HAVE_LAYER_SHELL
#include <gtk-layer-shell.h>
#endif

namespace kalwer {
inline GdkRectangle popup_geometry(GdkRectangle area, int width, int height) {
    return {std::max(area.x, area.x + area.width - width - 10),
            std::max(area.y, std::min(area.y + 96, area.y + area.height - height)),
            width, height};
}

inline GdkRectangle popup_beside_launcher(GdkRectangle area, GdkRectangle launcher,
                                          int width, int height, int anchor_x, int anchor_y) {
    return {std::clamp(launcher.x + anchor_x, area.x, std::max(area.x, area.x + area.width - width)),
            std::clamp(launcher.y + anchor_y, area.y, std::max(area.y, area.y + area.height - height)),
            width, height};
}

inline double popup_corner_progress(double elapsed, double expansion_finished, bool game) {
    if (!game) return 0.0;
    const double t = std::clamp((elapsed - expansion_finished) / 420.0, 0.0, 1.0);
    // Smooth departure and arrival, after the line and unfolding have finished.
    return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
}

inline GdkRectangle popup_interpolate(GdkRectangle start, GdkRectangle end, double progress) {
    const double t = std::clamp(progress, 0.0, 1.0);
    return {static_cast<int>(std::lround(start.x + (end.x - start.x) * t)),
            static_cast<int>(std::lround(start.y + (end.y - start.y) * t)), start.width, start.height};
}

inline GdkMonitor* popup_monitor(GtkWindow* launcher) {
    GdkDisplay* display = gtk_widget_get_display(GTK_WIDGET(launcher));
    if (GdkWindow* window = gtk_widget_get_window(GTK_WIDGET(launcher)))
        return gdk_display_get_monitor_at_window(display, window);
    GdkSeat* seat = gdk_display_get_default_seat(display);
    if (seat) {
        if (GdkDevice* pointer = gdk_seat_get_pointer(seat)) {
            int x = 0, y = 0;
            gdk_device_get_position(pointer, nullptr, &x, &y);
            return gdk_display_get_monitor_at_point(display, x, y);
        }
    }
    return gdk_display_get_primary_monitor(display);
}

struct PopupPlacement {
    GtkWindow* launcher;
    GdkMonitor* monitor;
    int anchor_x, anchor_y;
    double progress = 0.0;
    bool layer = false;
    int last_x = G_MININT, last_y = G_MININT;
    ~PopupPlacement() { g_object_unref(launcher); g_object_unref(monitor); }
};

inline void move_popup(GtkWindow* window) {
    auto* placement = static_cast<PopupPlacement*>(g_object_get_data(G_OBJECT(window), "kalwer-popup-placement"));
    if (!placement || gtk_widget_in_destruction(GTK_WIDGET(window))) return;
    GdkRectangle area{}, launcher{};
    gdk_monitor_get_workarea(placement->monitor, &area);
    gtk_window_get_size(placement->launcher, &launcher.width, &launcher.height);
    if (placement->layer) {
        // Native Wayland does not expose an xdg-toplevel's global position.
        // Kalwer requests centered placement for its launcher.
        launcher.x = area.x + (area.width - launcher.width) / 2;
        launcher.y = area.y + (area.height - launcher.height) / 2;
    } else {
        gtk_window_get_position(placement->launcher, &launcher.x, &launcher.y);
    }
    int width = 0, height = 0;
    gtk_window_get_size(window, &width, &height);
    const auto beside = popup_beside_launcher(area, launcher, width, height,
                                               placement->anchor_x, placement->anchor_y);
    const auto corner = popup_geometry(area, width, height);
    const auto position = popup_interpolate(beside, corner, placement->progress);
    if (position.x == placement->last_x && position.y == placement->last_y) return;
    placement->last_x = position.x; placement->last_y = position.y;
#ifdef KALWER_HAVE_LAYER_SHELL
    if (placement->layer) {
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_LEFT, position.x - area.x);
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_TOP, position.y - area.y);
        return;
    }
#endif
    gtk_window_move(window, position.x, position.y);
}

inline void animate_popup_position(GtkWindow* window, double progress) {
    auto* placement = static_cast<PopupPlacement*>(g_object_get_data(G_OBJECT(window), "kalwer-popup-placement"));
    if (!placement) return;
    placement->progress = progress;
    move_popup(window);
}

inline void place_output_popup(GtkWindow* window, GtkWindow* launcher, int anchor_x, int anchor_y) {
    GdkMonitor* monitor = popup_monitor(launcher);
    if (!monitor) return;
    auto* placement = new PopupPlacement{GTK_WINDOW(g_object_ref(launcher)),
        GDK_MONITOR(g_object_ref(monitor)), anchor_x, anchor_y};
    g_object_set_data_full(G_OBJECT(window), "kalwer-popup-placement", placement,
        +[](gpointer data) { delete static_cast<PopupPlacement*>(data); });
#ifdef KALWER_HAVE_LAYER_SHELL
    if (gtk_layer_is_supported()) {
        placement->layer = true;
        gtk_layer_init_for_window(window);
        gtk_layer_set_namespace(window, "kalwer-command-output");
        gtk_layer_set_monitor(window, monitor);
        gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_exclusive_zone(window, 0);
        gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    }
#endif
    gtk_window_set_position(window, GTK_WIN_POS_NONE);
    g_signal_connect(window, "map-event", G_CALLBACK(+[](GtkWidget* widget, GdkEvent*, gpointer) -> gboolean {
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer data) -> gboolean {
            auto* popup = GTK_WIDGET(data);
            if (gtk_widget_get_mapped(popup)) {
                auto* placement = static_cast<PopupPlacement*>(g_object_get_data(G_OBJECT(popup), "kalwer-popup-placement"));
                placement->last_x = G_MININT;
                move_popup(GTK_WINDOW(popup));
            }
            return G_SOURCE_REMOVE;
        }, g_object_ref(widget), g_object_unref);
        return FALSE;
    }), nullptr);
    g_signal_connect(window, "size-allocate", G_CALLBACK(+[](GtkWidget* widget, GtkAllocation*, gpointer) {
        move_popup(GTK_WINDOW(widget));
    }), nullptr);
    move_popup(window);
}
} // namespace kalwer
