#pragma once
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gtk/gtk.h>
#include <vector>
namespace kalwer::media {
struct Player {
  GtkWidget *canvas = nullptr;
  GtkWidget *button = nullptr;
  GtkWidget *message = nullptr;
  GstElement *pipeline = nullptr;
  GstElement *sink = nullptr;
  guint timer = 0, bus = 0;
  bool playing = false, preroll = false;
  std::vector<unsigned char> pixels;
  ~Player() {
    if (timer)
      g_source_remove(timer);
    if (bus)
      g_source_remove(bus);
    if (pipeline) {
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
    }
    if (sink)
      gst_object_unref(sink);
  }
  void pause() {
    playing = false;
    if (pipeline)
      gst_element_set_state(pipeline, GST_STATE_PAUSED);
    if (timer) {
      g_source_remove(timer);
      timer = 0;
    }
    if (button)
      gtk_button_set_label(GTK_BUTTON(button), "Play");
  }
  bool frame() {
    GstSample *sample =
        playing
            ? gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 0)
            : (!preroll ? gst_app_sink_try_pull_preroll(GST_APP_SINK(sink), 0)
                        : nullptr);
    if (sample) {
      auto buffer = gst_sample_get_buffer(sample);
      GstMapInfo mapped{};
      if (gst_buffer_map(buffer, &mapped, GST_MAP_READ)) {
        if (mapped.size >= 480 * 270 * 4) {
          pixels.assign(mapped.data, mapped.data + 480 * 270 * 4);
          gtk_widget_queue_draw(canvas);
          preroll = true;
        }
        gst_buffer_unmap(buffer, &mapped);
      }
      gst_sample_unref(sample);
    }
    return playing || !preroll;
  }
  void start_timer() {
    if (!timer)
      timer = g_timeout_add(
          40,
          +[](gpointer p) -> gboolean {
            auto player = static_cast<Player *>(p);
            if (!player->frame()) {
              player->timer = 0;
              return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
          },
          this);
  }
};
inline GtkWidget *create(const std::string &path) {
  auto type = g_content_type_guess(path.c_str(), nullptr, 0, nullptr);
  bool video = type && std::string(type).starts_with("video/");
  bool gif = type && std::string(type) == "image/gif";
  g_free(type);
  if (!video && !gif)
    return nullptr;
  gst_init(nullptr, nullptr);
  auto pipeline = gst_element_factory_make("playbin", nullptr);
  if (!pipeline)
    return nullptr;
  GError *error = nullptr;
  auto bin = gst_parse_bin_from_description(
      "videoconvert ! videoscale add-borders=true ! "
      "video/x-raw,format=BGRA,width=480,height=270,pixel-aspect-ratio=1/1 ! "
      "appsink name=frames max-buffers=1 drop=true sync=true",
      TRUE, &error);
  if (!bin) {
    g_clear_error(&error);
    gst_object_unref(pipeline);
    return nullptr;
  }
  gst_object_ref_sink(bin);
  gst_object_ref_sink(pipeline);
  auto root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  auto p = new Player;
  p->pipeline = pipeline;
  p->sink = gst_bin_get_by_name(GST_BIN(bin), "frames");
  auto uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
  g_object_set(pipeline, "uri", uri, "video-sink", bin, nullptr);
  g_free(uri);
  gst_object_unref(bin);
  p->canvas = gtk_drawing_area_new();
  gtk_widget_set_size_request(p->canvas, 300, 240);
  gtk_widget_set_hexpand(p->canvas, TRUE);
  gtk_widget_set_vexpand(p->canvas, TRUE);
  gtk_box_pack_start(GTK_BOX(root), p->canvas, TRUE, TRUE, 0);
  p->message = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(root), p->message, FALSE, FALSE, 0);
  p->button = gtk_button_new_with_label("Play");
  gtk_box_pack_start(GTK_BOX(root), p->button, FALSE, FALSE, 0);
  g_signal_connect(
      p->canvas, "draw",
      G_CALLBACK(
          (+[](GtkWidget *widget, cairo_t *cr, gpointer data) -> gboolean {
            auto p = static_cast<Player *>(data);
            if (p->pixels.empty())
              return FALSE;
            double w = gtk_widget_get_allocated_width(widget),
                   h = gtk_widget_get_allocated_height(widget),
                   scale = std::min(w / 480., h / 270.);
            auto frame = cairo_image_surface_create_for_data(
                p->pixels.data(), CAIRO_FORMAT_ARGB32, 480, 270, 480 * 4);
            cairo_save(cr);
            cairo_translate(cr, (w - 480 * scale) / 2, (h - 270 * scale) / 2);
            cairo_scale(cr, scale, scale);
            cairo_set_source_surface(cr, frame, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
            cairo_surface_destroy(frame);
            return FALSE;
          })),
      p);
  g_signal_connect(p->button, "clicked",
                   G_CALLBACK(+[](GtkWidget *, gpointer data) {
                     auto p = static_cast<Player *>(data);
                     if (p->playing)
                       p->pause();
                     else {
                       p->playing = true;
                       gtk_button_set_label(GTK_BUTTON(p->button), "Pause");
                       gst_element_set_state(p->pipeline, GST_STATE_PLAYING);
                       p->start_timer();
                     }
                   }),
                   p);
  g_signal_connect(root, "unmap", G_CALLBACK(+[](GtkWidget *, gpointer data) {
                     static_cast<Player *>(data)->pause();
                   }),
                   p);
  auto bus = gst_element_get_bus(pipeline);
  p->bus = gst_bus_add_watch(
      bus,
      +[](GstBus *, GstMessage *message, gpointer data) -> gboolean {
        auto p = static_cast<Player *>(data);
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
          gst_element_seek_simple(
              p->pipeline, GST_FORMAT_TIME,
              static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                        GST_SEEK_FLAG_KEY_UNIT),
              0);
        }
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
          p->pause();
          gtk_label_set_text(
              GTK_LABEL(p->message),
              "Preview unavailable. Use Open to view this file.");
        }
        return G_SOURCE_CONTINUE;
      },
      p);
  gst_object_unref(bus);
  g_object_set_data_full(
      G_OBJECT(root), "kalwer-media", p,
      +[](gpointer data) { delete static_cast<Player *>(data); });
  gst_element_set_state(pipeline, GST_STATE_PAUSED);
  p->start_timer();
  return root;
}
inline void pause_under(GtkWidget *widget) {
  if (!widget)
    return;
  if (auto p = static_cast<Player *>(
          g_object_get_data(G_OBJECT(widget), "kalwer-media")))
    p->pause();
  if (GTK_IS_CONTAINER(widget)) {
    auto children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (auto l = children; l; l = l->next)
      pause_under(GTK_WIDGET(l->data));
    g_list_free(children);
  }
}
} // namespace kalwer::media
