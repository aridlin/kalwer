#pragma once
#include <gtk/gtk.h>
#include <algorithm>
#ifdef KALWER_HAVE_LAYER_SHELL
#include <gtk-layer-shell.h>
#endif

namespace kalwer {
inline GdkRectangle popup_geometry(GdkRectangle area, int width, int height) {
    return {std::max(area.x, area.x + area.width - width - 10),
            std::max(area.y, std::min(area.y + 96, area.y + area.height - height)),
            width, height};
}

// GTK coordinates are logical pixels, including on scaled XWayland monitors.
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

inline void move_popup(GtkWindow* window) {
    auto* monitor = static_cast<GdkMonitor*>(
        g_object_get_data(G_OBJECT(window), "kalwer-popup-monitor"));
    if (!monitor || gtk_widget_in_destruction(GTK_WIDGET(window))) return;
    GdkRectangle area{};
    gdk_monitor_get_workarea(monitor, &area);
    int width = 0, height = 0;
    gtk_window_get_size(window, &width, &height);
    const auto position = popup_geometry(area, width, height);
    gtk_window_move(window, position.x, position.y);
}

inline void place_output_popup(GtkWindow* window, GtkWindow* launcher) {
    GdkMonitor* monitor = popup_monitor(launcher);
    if (!monitor) return;
#ifdef KALWER_HAVE_LAYER_SHELL
    if (gtk_layer_is_supported()) {
        gtk_layer_init_for_window(window);
        gtk_layer_set_namespace(window, "kalwer-command-output");
        gtk_layer_set_monitor(window, monitor);
        gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_TOP, 96);
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_RIGHT, 10);
        gtk_layer_set_exclusive_zone(window, 0);
        gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
        return;
    }
#endif
    // Wayland xdg-shell does not allow absolute window positioning. These
    // requests are honored on X11/XWayland; native Wayland uses layer-shell above.
    g_object_set_data_full(G_OBJECT(window), "kalwer-popup-monitor",
                          g_object_ref(monitor), g_object_unref);
    gtk_window_set_position(window, GTK_WIN_POS_NONE);
    g_signal_connect(window, "map-event", G_CALLBACK(+[](GtkWidget* widget, GdkEvent*, gpointer) -> gboolean {
        // Apply after the window manager's initial placement, not just before map.
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer data) -> gboolean {
            auto* popup = GTK_WIDGET(data);
            if (gtk_widget_get_mapped(popup)) move_popup(GTK_WINDOW(popup));
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
