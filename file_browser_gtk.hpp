#pragma once
#include "appearance.hpp"
#include <algorithm>
#include <deque>
#include <functional>
#include <gtk/gtk.h>
#include <string>
#include <vector>

namespace kalwer::files {
inline GtkWidget *window = nullptr;
inline std::function<void()> close_requested;
inline GtkWidget *toolbar, *browser_tools;
inline GtkWidget *stack, *grid, *location, *search, *places, *status,
    *preview_box, *preview_title, *bookmark;
inline std::string directory, selected, pending_preview;
inline std::vector<std::string> bookmarks;
inline unsigned generation = 0;
inline bool previewing = false;
inline GtkWidget *scroller = nullptr;
inline GCancellable *listing_cancel = nullptr;
inline guint viewport_idle = 0, append_idle = 0;
inline GtkCssProvider *file_css = nullptr;
inline void queue_visible();
enum { URI = 1, GNOME = 2 };
inline GtkTargetEntry targets[] = {
    {const_cast<char *>("text/uri-list"), 0, URI},
    {const_cast<char *>("x-special/gnome-copied-files"), 0, GNOME}};
inline std::string config() {
  return std::string(g_get_user_config_dir()) + "/kalwer/file-bookmarks";
}
inline std::string expand(std::string path) {
  if (path.size() > 1 && ((path.front() == '"' && path.back() == '"') ||
                          (path.front() == '\'' && path.back() == '\'')))
    path = path.substr(1, path.size() - 2);
  if (path.empty()) {
    auto downloads = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    path = downloads ? downloads : std::string(g_get_home_dir()) + "/Downloads";
  }
  if (path == "~" || path.starts_with("~/"))
    path = std::string(g_get_home_dir()) + path.substr(1);
  char *absolute = g_canonicalize_filename(path.c_str(), g_get_home_dir());
  path = absolute;
  g_free(absolute);
  return path;
}
inline std::string uri(const std::string &path) {
  char *u = g_filename_to_uri(path.c_str(), nullptr, nullptr);
  std::string r = u ? u : "";
  g_free(u);
  return r;
}
inline void transfer(GtkSelectionData *data, guint type,
                     const std::string &path) {
  auto u = uri(path);
  if (type == GNOME) {
    auto value = "copy\n" + u + "\n";
    gtk_selection_data_set(data, gtk_selection_data_get_target(data), 8,
                           reinterpret_cast<const guchar *>(value.data()),
                           value.size());
  } else {
    gchar *uris[] = {const_cast<char *>(u.c_str()), nullptr};
    gtk_selection_data_set_uris(data, uris);
  }
}
inline void copy() {
  if (selected.empty())
    return;
  if (!g_file_test(selected.c_str(), G_FILE_TEST_IS_REGULAR)) {
    gtk_label_set_text(GTK_LABEL(status), "File is no longer available.");
    return;
  }
  auto value = new std::string(selected);
  if (!gtk_clipboard_set_with_data(
          gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), targets,
          G_N_ELEMENTS(targets),
          +[](GtkClipboard *, GtkSelectionData *d, guint type, gpointer p) {
            transfer(d, type, *static_cast<std::string *>(p));
          },
          +[](GtkClipboard *, gpointer p) {
            delete static_cast<std::string *>(p);
          },
          value))
    delete value;
}
inline void drag(GtkWidget *widget, const std::string &path) {
  g_object_set_data_full(G_OBJECT(widget), "file-path", g_strdup(path.c_str()),
                         g_free);
  gtk_drag_source_set(widget, GDK_BUTTON1_MASK, targets, G_N_ELEMENTS(targets),
                      GDK_ACTION_COPY);
  g_signal_connect(
      widget, "drag-data-get",
      G_CALLBACK(+[](GtkWidget *w, GdkDragContext *, GtkSelectionData *d,
                     guint type, guint, gpointer) {
        transfer(d, type,
                 static_cast<const char *>(
                     g_object_get_data(G_OBJECT(w), "file-path")));
      }),
      nullptr);
}
struct ImageWork {
  GWeakRef image;
  std::string path;
  int size;
  unsigned generation;
  guint64 modified_us;
};
inline std::deque<ImageWork *> image_queue;
inline unsigned image_active = 0, image_peak = 0, image_started = 0;
struct CachedImage {
  std::string key;
  GdkPixbuf *pix;
};
inline std::deque<CachedImage> image_cache;
inline std::string image_key(const ImageWork *w) {
  return w->path + "\n" + std::to_string(w->modified_us);
}
inline void dispose_image(ImageWork *w) {
  g_weak_ref_clear(&w->image);
  delete w;
}
inline bool on_screen(GtkWidget *image) {
  if (!window || previewing || !scroller || !gtk_widget_get_mapped(image))
    return false;
  int x = 0, y = 0;
  if (!gtk_widget_translate_coordinates(image, scroller, 0, 0, &x, &y))
    return false;
  return y + gtk_widget_get_allocated_height(image) > 0 &&
         y < gtk_widget_get_allocated_height(scroller);
}
inline void pump_images();
inline std::string cached_thumbnail(const std::string &path) {
  auto u = uri(path);
  auto hash = g_compute_checksum_for_string(G_CHECKSUM_MD5, u.c_str(), -1);
  std::string found;
  for (const char *size : {"x-large", "large", "normal"}) {
    auto candidate = std::string(g_get_user_cache_dir()) + "/thumbnails/" +
                     size + "/" + hash + ".png";
    if (g_file_test(candidate.c_str(), G_FILE_TEST_IS_REGULAR)) {
      found = candidate;
      break;
    }
  }
  g_free(hash);
  return found;
}
inline void pump_images() {
  while (image_active < 2 && !image_queue.empty()) {
    auto w = image_queue.front();
    image_queue.pop_front();
    auto image = g_weak_ref_get(&w->image);
    bool valid = image && w->generation == generation &&
                 (w->size > 96 || on_screen(GTK_WIDGET(image)));
    if (!valid) {
      if (image) {
        g_object_set_data(G_OBJECT(image), "thumbnail-requested", nullptr);
        g_object_unref(image);
      }
      dispose_image(w);
      continue;
    }
    if (w->size == 96) {
      auto key = image_key(w);
      auto found = std::find_if(image_cache.begin(), image_cache.end(),
                                [&](const auto &e) { return e.key == key; });
      if (found != image_cache.end()) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), found->pix);
        auto hit = *found;
        image_cache.erase(found);
        image_cache.push_back(hit);
        g_object_unref(image);
        dispose_image(w);
        continue;
      }
    }
    g_object_unref(image);
    ++image_active;
    ++image_started;
    image_peak = std::max(image_peak, image_active);
    auto task = g_task_new(
        nullptr, nullptr,
        +[](GObject *, GAsyncResult *result, gpointer) {
          auto w =
              static_cast<ImageWork *>(g_task_get_task_data(G_TASK(result)));
          auto pix = static_cast<GdkPixbuf *>(
              g_task_propagate_pointer(G_TASK(result), nullptr));
          auto image = g_weak_ref_get(&w->image);
          if (image) {
            if (pix && w->generation == generation)
              gtk_image_set_from_pixbuf(GTK_IMAGE(image), pix);
            g_object_unref(image);
          }
          if (pix) {
            if (w->size == 96) {
              image_cache.push_back(
                  {image_key(w), GDK_PIXBUF(g_object_ref(pix))});
              while (image_cache.size() > 64) {
                g_object_unref(image_cache.front().pix);
                image_cache.pop_front();
              }
            }
            g_object_unref(pix);
          }
          --image_active;
          pump_images();
        },
        nullptr);
    g_task_set_task_data(
        task, w,
        +[](gpointer p) { dispose_image(static_cast<ImageWork *>(p)); });
    g_task_run_in_thread(
        task, +[](GTask *task, gpointer, gpointer data, GCancellable *) {
          auto w = static_cast<ImageWork *>(data);
          std::string path;
          if (w->size == 96)
            path = cached_thumbnail(w->path);
          if (path.empty()) {
            auto type =
                g_content_type_guess(w->path.c_str(), nullptr, 0, nullptr);
            if (type && std::string(type).starts_with("image/"))
              path = w->path;
            else if (w->size > 96)
              path = cached_thumbnail(w->path);
            g_free(type);
          }
          auto pix = path.empty()
                         ? nullptr
                         : gdk_pixbuf_new_from_file_at_scale(
                               path.c_str(), w->size, w->size, TRUE, nullptr);
          if (pix) {
            auto oriented = gdk_pixbuf_apply_embedded_orientation(pix);
            g_object_unref(pix);
            pix = oriented;
          }
          g_task_return_pointer(task, pix, pix ? g_object_unref : nullptr);
        });
    g_object_unref(task);
  }
}
inline void image_async(GtkWidget *image, const std::string &path, int size,
                        guint64 modified_us = 0) {
  auto w = new ImageWork{{}, path, size, generation, modified_us};
  g_weak_ref_init(&w->image, G_OBJECT(image));
  if (size > 96)
    image_queue.push_front(w);
  else
    image_queue.push_back(w);
  pump_images();
}
struct TextWork {
  GWeakRef buffer;
  std::string path;
};
inline void text_async(GtkWidget *view, const std::string &path) {
  auto w = new TextWork{{}, path};
  g_weak_ref_init(&w->buffer,
                  G_OBJECT(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view))));
  auto task = g_task_new(
      nullptr, nullptr,
      +[](GObject *, GAsyncResult *result, gpointer) {
        auto w = static_cast<TextWork *>(g_task_get_task_data(G_TASK(result)));
        auto text = static_cast<gchar *>(
            g_task_propagate_pointer(G_TASK(result), nullptr));
        auto buffer = g_weak_ref_get(&w->buffer);
        if (buffer) {
          if (text)
            gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer), text, -1);
          g_object_unref(buffer);
        }
        g_free(text);
      },
      nullptr);
  g_task_set_task_data(
      task, w, +[](gpointer p) {
        auto w = static_cast<TextWork *>(p);
        g_weak_ref_clear(&w->buffer);
        delete w;
      });
  g_task_run_in_thread(
      task, +[](GTask *task, gpointer, gpointer data, GCancellable *) {
        auto w = static_cast<TextWork *>(data);
        auto file = g_file_new_for_path(w->path.c_str());
        auto stream = g_file_read(file, nullptr, nullptr);
        g_object_unref(file);
        gchar *text = nullptr;
        if (stream) {
          gchar bytes[65537];
          gsize length = 0;
          if (g_input_stream_read_all(G_INPUT_STREAM(stream), bytes, 65536,
                                      &length, nullptr, nullptr))
            text = g_utf8_make_valid(bytes, length);
          g_object_unref(stream);
        }
        g_task_return_pointer(task, text, g_free);
      });
  g_object_unref(task);
}
inline void clear(GtkWidget *box) {
  auto children = gtk_container_get_children(GTK_CONTAINER(box));
  for (auto p = children; p; p = p->next)
    gtk_widget_destroy(GTK_WIDGET(p->data));
  g_list_free(children);
}
inline void browse(std::string path);
inline void back() {
  previewing = false;
  gtk_stack_set_visible_child_name(GTK_STACK(stack), "files");
  gtk_widget_show(toolbar);
  gtk_widget_show(browser_tools);
  gtk_widget_grab_focus(grid);
  queue_visible();
}
inline void show_file(const std::string &path) {
  if (!g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR)) {
    gtk_label_set_text(GTK_LABEL(status), "File is no longer available.");
    return;
  }
  selected = path;
  previewing = true;
  gtk_widget_hide(toolbar);
  gtk_widget_hide(browser_tools);
  clear(preview_box);
  g_object_set_data_full(G_OBJECT(preview_title), "file-path",
                         g_strdup(path.c_str()), g_free);
  char *base = g_filename_display_basename(path.c_str());
  gtk_label_set_text(GTK_LABEL(preview_title), base);
  g_free(base);
  auto event = gtk_event_box_new();
  auto image =
      gtk_image_new_from_icon_name("text-x-generic", GTK_ICON_SIZE_DIALOG);
  gtk_widget_set_can_focus(event, TRUE);
  gtk_container_add(GTK_CONTAINER(event), image);
  gtk_box_pack_start(GTK_BOX(preview_box), event, TRUE, TRUE, 0);
  drag(event, path);
  auto type = g_content_type_guess(path.c_str(), nullptr, 0, nullptr);
  if (type && g_content_type_is_a(type, "text/plain")) {
    auto scroll = gtk_scrolled_window_new(nullptr, nullptr);
    auto view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(preview_box), scroll, TRUE, TRUE, 0);
    gtk_widget_destroy(event);
    text_async(view, path);
  } else
    image_async(image, path, 280);
  g_free(type);
  auto detail = gtk_label_new(path.c_str());
  gtk_label_set_ellipsize(GTK_LABEL(detail), PANGO_ELLIPSIZE_MIDDLE);
  gtk_label_set_selectable(GTK_LABEL(detail), TRUE);
  gtk_box_pack_end(GTK_BOX(preview_box), detail, FALSE, FALSE, 4);
  gtk_widget_show_all(preview_box);
  gtk_stack_set_visible_child_name(GTK_STACK(stack), "preview");
  gtk_widget_grab_focus(preview_box);
}
inline void refresh_places() {
  clear(places);
  auto add = [](const std::string &path, const std::string &label) {
    auto b = gtk_button_new_with_label(label.c_str());
    gtk_style_context_add_class(gtk_widget_get_style_context(b), "file-place");
    if (path == directory)
      gtk_style_context_add_class(gtk_widget_get_style_context(b), "current");
    g_object_set_data_full(G_OBJECT(b), "path", g_strdup(path.c_str()), g_free);
    g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkWidget *b, gpointer) {
                       browse(static_cast<const char *>(
                           g_object_get_data(G_OBJECT(b), "path")));
                     }),
                     nullptr);
    gtk_box_pack_start(GTK_BOX(places), b, FALSE, FALSE, 0);
  };
  add(g_get_home_dir(), "Home");
  for (const auto &path : bookmarks) {
    char *name = g_filename_display_basename(path.c_str());
    add(path, name);
    g_free(name);
  }
  bool saved = std::find(bookmarks.begin(), bookmarks.end(), directory) !=
               bookmarks.end();
  gtk_button_set_image(GTK_BUTTON(bookmark),
                       gtk_image_new_from_icon_name(
                           saved ? "starred-symbolic" : "non-starred-symbolic",
                           GTK_ICON_SIZE_MENU));
  auto style = gtk_widget_get_style_context(bookmark);
  if (saved)
    gtk_style_context_add_class(style, "saved");
  else
    gtk_style_context_remove_class(style, "saved");
  gtk_widget_show_all(places);
}
struct Entry {
  std::string path, name, icon, thumbnail;
  bool folder;
  guint64 modified_us = 0;
  std::string folded;
};
struct Listing {
  unsigned generation;
  std::string path, error;
  std::vector<Entry> entries;
};
inline std::vector<Entry> entries;
inline std::vector<size_t> matches;
inline size_t rendered = 0;
inline constexpr size_t page_size = 48;
inline bool folders_first(const std::string &path) {
  return expand(path) != expand("");
}
inline void sort_entries(std::vector<Entry> &items, bool folders) {
  std::sort(items.begin(), items.end(),
            [folders](const auto &a, const auto &b) {
              if (folders && a.folder != b.folder)
                return a.folder > b.folder;
              if (a.modified_us != b.modified_us)
                return a.modified_us > b.modified_us;
              return g_utf8_collate(a.name.c_str(), b.name.c_str()) < 0;
            });
}
inline void append_page() {
  const auto end = std::min(rendered + page_size, matches.size());
  for (; rendered < end; ++rendered) {
    const auto &e = entries[matches[rendered]];
    auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    auto image =
        gtk_image_new_from_icon_name(e.icon.c_str(), GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_size_request(image, 96, 84);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
    auto label = gtk_label_new(e.name.c_str());
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 16);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    auto tile = gtk_event_box_new();
    gtk_event_box_set_above_child(GTK_EVENT_BOX(tile), TRUE);
    gtk_container_add(GTK_CONTAINER(tile), box);
    gtk_flow_box_insert(GTK_FLOW_BOX(grid), tile, -1);
    auto child = gtk_widget_get_parent(tile);
    g_object_set_data_full(G_OBJECT(child), "name", g_strdup(e.name.c_str()),
                           g_free);
    g_object_set_data_full(G_OBJECT(child), "path", g_strdup(e.path.c_str()),
                           g_free);
    gtk_widget_set_tooltip_text(child, e.path.c_str());
    if (!e.folder)
      drag(tile, e.path);
    g_signal_connect(tile, "button-press-event",
                     G_CALLBACK(+[](GtkWidget *tile, GdkEventButton *event,
                                    gpointer) -> gboolean {
                       if (event->button != 1)
                         return FALSE;
                       auto child = gtk_widget_get_parent(tile);
                       auto flow = gtk_widget_get_parent(child);
                       gtk_flow_box_select_child(GTK_FLOW_BOX(flow),
                                                 GTK_FLOW_BOX_CHILD(child));
                       gtk_widget_grab_focus(child);
                       if (event->type == GDK_2BUTTON_PRESS)
                         g_signal_emit_by_name(flow, "child-activated", child);
                       return FALSE;
                     }),
                     nullptr);
    if (!e.folder) {
      g_object_set_data_full(G_OBJECT(image), "thumbnail-path",
                             g_strdup(e.path.c_str()), g_free);
      auto stamp = new guint64(e.modified_us);
      g_object_set_data_full(
          G_OBJECT(image), "modified-us", stamp,
          +[](gpointer p) { delete static_cast<guint64 *>(p); });
    }
  }
  gtk_widget_show_all(grid);
  queue_visible();
}
inline void refill() {
  if (!window)
    return;
  clear(grid);
  rendered = 0;
  matches.clear();
  auto needle = g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(search)), -1);
  for (size_t i = 0; i < entries.size(); ++i)
    if (entries[i].folded.find(needle) != std::string::npos)
      matches.push_back(i);
  g_free(needle);
  auto adjustment =
      gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller));
  gtk_adjustment_set_value(adjustment, 0);
  append_page();
  auto first = gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(grid), 0);
  if (first) {
    gtk_flow_box_select_child(GTK_FLOW_BOX(grid), first);
    if (!gtk_widget_has_focus(search))
      gtk_widget_grab_focus(GTK_WIDGET(first));
  }
  gtk_label_set_text(GTK_LABEL(status), entries.empty()   ? "Folder is empty."
                                        : matches.empty() ? "No matching files."
                                                          : "");
}
inline void queue_visible() {
  if (viewport_idle || !window)
    return;
  viewport_idle = g_idle_add(
      +[](gpointer) -> gboolean {
        viewport_idle = 0;
        if (!window || previewing)
          return G_SOURCE_REMOVE;
        auto children = gtk_container_get_children(GTK_CONTAINER(grid));
        for (auto p = children; p; p = p->next) {
          auto tile = gtk_bin_get_child(GTK_BIN(p->data));
          auto box = gtk_bin_get_child(GTK_BIN(tile));
          auto kids = gtk_container_get_children(GTK_CONTAINER(box));
          auto image = kids ? GTK_WIDGET(kids->data) : nullptr;
          g_list_free(kids);
          if (!image || !on_screen(image) ||
              g_object_get_data(G_OBJECT(image), "thumbnail-requested"))
            continue;
          auto path = static_cast<const char *>(
              g_object_get_data(G_OBJECT(image), "thumbnail-path"));
          if (path) {
            g_object_set_data(G_OBJECT(image), "thumbnail-requested",
                              GINT_TO_POINTER(1));
            auto stamp = static_cast<guint64 *>(
                g_object_get_data(G_OBJECT(image), "modified-us"));
            image_async(image, path, 96, stamp ? *stamp : 0);
          }
        }
        g_list_free(children);
        return G_SOURCE_REMOVE;
      },
      nullptr);
}
inline void scroll_changed() {
  queue_visible();
  if (!window || append_idle || rendered >= matches.size())
    return;
  auto a = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller));
  auto remaining = gtk_adjustment_get_upper(a) - gtk_adjustment_get_value(a) -
                   gtk_adjustment_get_page_size(a);
  if (remaining < gtk_adjustment_get_page_size(a))
    append_idle = g_idle_add(
        +[](gpointer) -> gboolean {
          append_idle = 0;
          if (window && !previewing)
            append_page();
          return G_SOURCE_REMOVE;
        },
        nullptr);
}
inline void browse(std::string path) {
  pending_preview.clear();
  path = expand(path);
  if (g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR)) {
    show_file(path);
    return;
  }
  auto request = ++generation;
  if (listing_cancel)
    g_cancellable_cancel(listing_cancel);
  g_clear_object(&listing_cancel);
  listing_cancel = g_cancellable_new();
  while (!image_queue.empty()) {
    dispose_image(image_queue.front());
    image_queue.pop_front();
  }
  auto w = new Listing{request, path, {}, {}};
  auto task = g_task_new(
      nullptr, listing_cancel,
      +[](GObject *, GAsyncResult *result, gpointer) {
        auto w = static_cast<Listing *>(g_task_get_task_data(G_TASK(result)));
        if (!window || w->generation != generation)
          return;
        if (!w->error.empty()) {
          gtk_label_set_text(GTK_LABEL(status), w->error.c_str());
          return;
        }
        directory = w->path;
        selected.clear();
        back();
        entries.clear();
        clear(grid);
        auto shown =
            directory.starts_with(std::string(g_get_home_dir()) + "/")
                ? "~" + directory.substr(
                            std::char_traits<char>::length(g_get_home_dir()))
                : directory;
        gtk_entry_set_text(GTK_ENTRY(location), shown.c_str());
        gtk_entry_set_text(GTK_ENTRY(search), "");
        refresh_places();
        entries = std::move(w->entries);
        refill();
        if (!pending_preview.empty()) {
          auto path = pending_preview;
          pending_preview.clear();
          show_file(path);
        }
      },
      nullptr);
  g_task_set_task_data(
      task, w, +[](gpointer p) { delete static_cast<Listing *>(p); });
  g_task_run_in_thread(
      task, +[](GTask *task, gpointer, gpointer data, GCancellable *cancel) {
        auto w = static_cast<Listing *>(data);
        auto file = g_file_new_for_path(w->path.c_str());
        GError *error = nullptr;
        auto enumerator = g_file_enumerate_children(
            file,
            "standard::name,standard::display-name,standard::type,"
            "time::modified,time::"
            "modified-usec",
            G_FILE_QUERY_INFO_NONE, cancel, &error);
        if (enumerator) {
          while (auto info =
                     g_file_enumerator_next_file(enumerator, cancel, &error)) {
            const char *name = g_file_info_get_name(info);
            if (name[0] != '.') {
              bool folder =
                  g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
              auto p = g_file_get_child(file, name);
              char *full = g_file_get_path(p);
              auto type = g_content_type_guess(name, nullptr, 0, nullptr);
              auto icon = type ? g_content_type_get_icon(type) : nullptr;
              char *icon_name = icon ? g_icon_to_string(icon) : nullptr;
              Entry e{full ? full : "",
                      g_file_info_get_display_name(info),
                      folder ? "folder" : "text-x-generic",
                      "",
                      folder,
                      g_file_info_get_attribute_uint64(
                          info, G_FILE_ATTRIBUTE_TIME_MODIFIED) *
                              G_USEC_PER_SEC +
                          g_file_info_get_attribute_uint32(
                              info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC),
                      {}};
              if (!folder && icon_name && G_IS_THEMED_ICON(icon)) {
                auto names = g_themed_icon_get_names(G_THEMED_ICON(icon));
                if (names && names[0])
                  e.icon = names[0];
              }
              auto folded = g_utf8_casefold(e.name.c_str(), -1);
              e.folded = folded;
              g_free(folded);
              g_free(type);
              if (icon)
                g_object_unref(icon);
              w->entries.push_back(std::move(e));
              g_free(full);
              g_free(icon_name);
              g_object_unref(p);
            }
            g_object_unref(info);
          }
          g_object_unref(enumerator);
        }
        if (error) {
          w->error = error->message;
          g_error_free(error);
        }
        g_object_unref(file);
        sort_entries(w->entries, folders_first(w->path));
        g_task_return_boolean(task, TRUE);
      });
  g_object_unref(task);
}
inline GtkWidget *button(GtkWidget *box, const char *text, GCallback callback) {
  auto b = gtk_button_new_with_label(text);
  gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
  g_signal_connect(b, "clicked", callback, nullptr);
  return b;
}
inline void first_match(bool activate) {
  auto children = gtk_container_get_children(GTK_CONTAINER(grid));
  for (auto p = children; p; p = p->next)
    if (gtk_widget_get_child_visible(GTK_WIDGET(p->data))) {
      gtk_flow_box_select_child(GTK_FLOW_BOX(grid),
                                GTK_FLOW_BOX_CHILD(p->data));
      gtk_widget_grab_focus(GTK_WIDGET(p->data));
      if (activate)
        g_signal_emit_by_name(grid, "child-activated", p->data);
      break;
    }
  g_list_free(children);
}
inline GtkWidget *icon_button(GtkWidget *box, const char *icon,
                              const char *tooltip, GCallback callback) {
  auto b = button(box, "", callback);
  gtk_button_set_image(GTK_BUTTON(b),
                       gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_MENU));
  gtk_button_set_always_show_image(GTK_BUTTON(b), TRUE);
  gtk_widget_set_tooltip_text(b, tooltip);
  gtk_style_context_add_class(gtk_widget_get_style_context(b), "file-icon");
  return b;
}
inline gboolean key(GdkEventKey *e) {
  if (e->keyval == GDK_KEY_Escape) {
    if (previewing)
      back();
    else
      close_requested();
    return TRUE;
  }
  if (!previewing && (e->state & GDK_CONTROL_MASK) &&
      (e->keyval == GDK_KEY_f || e->keyval == GDK_KEY_l)) {
    gtk_widget_grab_focus(e->keyval == GDK_KEY_f ? search : location);
    return TRUE;
  }
  if (!previewing && gtk_widget_has_focus(search) &&
      (e->keyval == GDK_KEY_Down || e->keyval == GDK_KEY_Return)) {
    auto children = gtk_container_get_children(GTK_CONTAINER(grid));
    for (auto p = children; p; p = p->next)
      if (gtk_widget_get_child_visible(GTK_WIDGET(p->data))) {
        gtk_flow_box_select_child(GTK_FLOW_BOX(grid),
                                  GTK_FLOW_BOX_CHILD(p->data));
        gtk_widget_grab_focus(GTK_WIDGET(p->data));
        if (e->keyval == GDK_KEY_Return)
          g_signal_emit_by_name(grid, "child-activated", p->data);
        break;
      }
    g_list_free(children);
    return TRUE;
  }
  if (previewing && (e->state & GDK_CONTROL_MASK) &&
      (e->keyval == GDK_KEY_c || e->keyval == GDK_KEY_C)) {
    copy();
    return TRUE;
  }
  if (!previewing && !gtk_widget_has_focus(search) &&
      !gtk_widget_has_focus(location) &&
      !(e->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))) {
    if (e->keyval == GDK_KEY_BackSpace) {
      auto parent = g_path_get_dirname(directory.c_str());
      browse(parent);
      g_free(parent);
      return TRUE;
    }
    auto ch = gdk_keyval_to_unicode(e->keyval);
    if (ch && g_unichar_isprint(ch)) {
      char bytes[8]{};
      g_unichar_to_utf8(ch, bytes);
      gtk_widget_grab_focus(search);
      gtk_entry_set_text(GTK_ENTRY(search), bytes);
      gtk_editable_set_position(GTK_EDITABLE(search), -1);
      return TRUE;
    }
  }
  return FALSE;
}
inline GtkWidget *create(GtkWidget *host, std::string path,
                         std::function<void()> close) {
  window = host;
  gtk_style_context_add_class(gtk_widget_get_style_context(host),
                              "kalwer-file-host");
  close_requested = std::move(close);
  auto root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_name(root, "kalwer-file-content");
  gtk_container_set_border_width(GTK_CONTAINER(root), 4);
  toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 0);
  icon_button(toolbar, "go-up-symbolic", "Parent folder (Backspace)",
              G_CALLBACK(+[](GtkWidget *, gpointer) {
                auto parent = g_path_get_dirname(directory.c_str());
                browse(parent);
                g_free(parent);
              }));
  location = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(toolbar), location, TRUE, TRUE, 0);
  g_signal_connect(location, "activate", G_CALLBACK(+[](GtkWidget *, gpointer) {
                     browse(gtk_entry_get_text(GTK_ENTRY(location)));
                   }),
                   nullptr);
  bookmark = icon_button(
      toolbar, "non-starred-symbolic", "Bookmark this folder",
      G_CALLBACK(+[](GtkWidget *, gpointer) {
        auto it = std::find(bookmarks.begin(), bookmarks.end(), directory);
        if (it == bookmarks.end())
          bookmarks.push_back(directory);
        else
          bookmarks.erase(it);
        auto parent = g_path_get_dirname(config().c_str());
        g_mkdir_with_parents(parent, 0700);
        g_free(parent);
        gchar **values = g_new0(gchar *, bookmarks.size() + 1);
        for (size_t i = 0; i < bookmarks.size(); ++i)
          values[i] = g_strdup(bookmarks[i].c_str());
        GKeyFile *key = g_key_file_new();
        g_key_file_set_string_list(key, "files", "bookmarks",
                                   const_cast<const gchar *const *>(values),
                                   bookmarks.size());
        GError *error = nullptr;
        g_key_file_save_to_file(key, config().c_str(), &error);
        if (error) {
          gtk_label_set_text(GTK_LABEL(status), error->message);
          g_error_free(error);
        }
        g_key_file_unref(key);
        g_strfreev(values);
        refresh_places();
      }));
  gtk_widget_set_tooltip_text(bookmark, "Bookmark this folder");
  icon_button(toolbar, "window-close-symbolic", "Close (Esc)",
              G_CALLBACK(+[](GtkWidget *, gpointer) { close_requested(); }));
  browser_tools = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_box_pack_start(GTK_BOX(root), browser_tools, FALSE, FALSE, 0);
  auto bookmark_scroll = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(bookmark_scroll),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
  places = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_container_add(GTK_CONTAINER(bookmark_scroll), places);
  gtk_box_pack_start(GTK_BOX(browser_tools), bookmark_scroll, FALSE, FALSE, 0);
  stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(stack),
                                GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_set_transition_duration(GTK_STACK(stack), 140);
  gtk_box_pack_start(GTK_BOX(root), stack, TRUE, TRUE, 0);
  auto browser = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_stack_add_named(GTK_STACK(stack), browser, "files");
  auto content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start(GTK_BOX(browser), content, TRUE, TRUE, 0);
  search = gtk_search_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Find a file…");
  gtk_box_pack_start(GTK_BOX(browser_tools), search, FALSE, FALSE, 0);
  g_signal_connect(search, "activate", G_CALLBACK(+[](GtkWidget *, gpointer) {
                     refill();
                     first_match(true);
                   }),
                   nullptr);
  auto scroll = gtk_scrolled_window_new(nullptr, nullptr);
  scroller = scroll;
  auto adjustment =
      gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
  g_signal_connect(
      adjustment, "value-changed",
      G_CALLBACK(+[](GtkAdjustment *, gpointer) { scroll_changed(); }),
      nullptr);
  g_signal_connect(
      adjustment, "changed",
      G_CALLBACK(+[](GtkAdjustment *, gpointer) { queue_visible(); }), nullptr);
  gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
  grid = gtk_flow_box_new();
  gtk_widget_set_valign(grid, GTK_ALIGN_START);
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(grid), GTK_SELECTION_SINGLE);
  gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(grid), FALSE);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid), 4);
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid), 3);
  g_signal_connect(grid, "size-allocate",
                   G_CALLBACK(+[](GtkWidget *, GtkAllocation *, gpointer) {
                     queue_visible();
                   }),
                   nullptr);
  gtk_container_add(GTK_CONTAINER(scroll), grid);
  g_signal_connect(search, "search-changed",
                   G_CALLBACK(+[](GtkWidget *, gpointer) { refill(); }),
                   nullptr);
  g_signal_connect(
      grid, "child-activated",
      G_CALLBACK(+[](GtkFlowBox *, GtkFlowBoxChild *child, gpointer) {
        browse(static_cast<const char *>(
            g_object_get_data(G_OBJECT(child), "path")));
      }),
      nullptr);
  auto preview = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_stack_add_named(GTK_STACK(stack), preview, "preview");
  auto actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start(GTK_BOX(preview), actions, FALSE, FALSE, 0);
  preview_title = gtk_label_new("");
  drag(preview_title, "");
  gtk_label_set_ellipsize(GTK_LABEL(preview_title), PANGO_ELLIPSIZE_MIDDLE);
  gtk_box_pack_start(GTK_BOX(actions), preview_title, TRUE, TRUE, 0);
  auto copy_button = button(actions, "Copy",
                            G_CALLBACK(+[](GtkWidget *, gpointer) { copy(); }));
  gtk_button_set_image(
      GTK_BUTTON(copy_button),
      gtk_image_new_from_icon_name("edit-copy-symbolic", GTK_ICON_SIZE_MENU));
  gtk_button_set_always_show_image(GTK_BUTTON(copy_button), TRUE);
  gtk_widget_set_tooltip_text(copy_button, "Copy file (Ctrl+C)");
  icon_button(actions, "window-close-symbolic", "Back to files (Esc)",
              G_CALLBACK(+[](GtkWidget *, gpointer) { back(); }));
  preview_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_can_focus(preview_box, TRUE);
  gtk_box_pack_start(GTK_BOX(preview), preview_box, TRUE, TRUE, 0);
  status = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(root), status, FALSE, FALSE, 0);
  g_signal_connect(
      window, "destroy", G_CALLBACK(+[](GtkWidget *host, gpointer) {
        window = nullptr;
        ++generation;
        if (listing_cancel)
          g_cancellable_cancel(listing_cancel);
        g_clear_object(&listing_cancel);
        if (viewport_idle)
          g_source_remove(viewport_idle);
        viewport_idle = 0;
        if (append_idle)
          g_source_remove(append_idle);
        append_idle = 0;
        while (!image_queue.empty()) {
          dispose_image(image_queue.front());
          image_queue.pop_front();
        }
        entries.clear();
        matches.clear();
        rendered = 0;
        if (file_css) {
          gtk_style_context_remove_provider_for_screen(
              gtk_widget_get_screen(host), GTK_STYLE_PROVIDER(file_css));
          g_clear_object(&file_css);
        }
        directory.clear();
        selected.clear();
        previewing = false;
        pending_preview.clear();
      }),
      nullptr);
  bookmarks.clear();
  auto key = g_key_file_new();
  if (g_key_file_load_from_file(key, config().c_str(), G_KEY_FILE_NONE,
                                nullptr)) {
    gsize length = 0;
    auto values =
        g_key_file_get_string_list(key, "files", "bookmarks", &length, nullptr);
    for (gsize i = 0; i < length; ++i)
      bookmarks.emplace_back(values[i]);
    g_strfreev(values);
  }
  g_key_file_unref(key);
  auto css = gtk_css_provider_new();
  gtk_css_provider_load_from_data(
      css,
      theme_css(
          "#kalwer-file-content, #kalwer-file-content *, .kalwer-file-host, "
          ".kalwer-file-host * "
          "{-GtkWidget-window-dragging:false;} "
          "#kalwer-file-content "
          "{background:#00130b;color:#cfe3d2;font-family:'JetBrainsMono "
          "Nerd Font',monospace;font-size:10px;} "
          "#kalwer-file-content label {color:#cfe3d2;} "
          "#kalwer-file-content button,#kalwer-file-content entry "
          "{background:#002215;color:#cfe3d2;border:1px solid "
          "#315f43;border-radius:5px;box-shadow:none;min-height:24px;"
          "padding:2px 7px;font-size:10px;} "
          "#kalwer-file-content button {background-image:linear-gradient(to "
          "bottom,#073321,#002215);border-color:#417756;box-shadow:inset 0 1px "
          "rgba(158,232,180,0.08);transition:background-color 130ms "
          "ease-out,border-color 130ms ease-out,box-shadow 130ms "
          "ease-out,color 130ms ease-out;} "
          "#kalwer-file-content button.file-icon {min-width:24px;padding:2px;} "
          "#kalwer-file-content button.file-place {min-height:20px;padding:1px "
          "7px;border-radius:10px;background-image:none;} "
          "#kalwer-file-content button.current,#kalwer-file-content "
          "button.saved {background-color:#0e4726;border-color:#9ee8b4;} "
          "#kalwer-file-content button:hover "
          "{background-image:none;background-color:#0e4726;border-color:#"
          "9ee8b4;box-shadow:inset 0 0 0 1px rgba(158,232,180,0.12),0 0 4px "
          "rgba(117,209,145,0.15);} "
          "#kalwer-file-content button:active "
          "{background-image:none;background-color:#1f6b3e;border-color:#"
          "cfe3d2;box-shadow:inset 0 2px 3px "
          "rgba(0,19,11,0.8);transition-duration:60ms;} "
          "#kalwer-file-content button:focus {border-color:#cfe3d2;} "
          "#kalwer-file-content entry:focus {border-color:#9ee8b4;} "
          "#kalwer-file-content flowbox {background:transparent;} "
          "#kalwer-file-content flowboxchild {padding:5px;border:1px "
          "solid transparent;border-radius:5px;transition:background-color "
          "120ms ease-out,border-color 120ms ease-out;} "
          "#kalwer-file-content flowboxchild:selected "
          "{background:#073c23;border-color:#75d191;} "
          "#kalwer-file-content flowboxchild:hover {background:#082f1e;} "
          "#kalwer-file-content textview text "
          "{background:#00130b;color:#cfe3d2;} #kalwer-file-content "
          "entry selection {background:#256c43;} "
          "#kalwer-file-content scrollbar slider "
          "{background:#315f43;min-width:4px;min-height:20px;}")
          .c_str(),
      -1, nullptr);
  gtk_style_context_add_provider_for_screen(
      gtk_widget_get_screen(window), GTK_STYLE_PROVIDER(css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  file_css = css;
  gtk_widget_show_all(root);
  auto initial = expand(path);
  if (g_file_test(initial.c_str(), G_FILE_TEST_IS_REGULAR)) {
    auto parent = g_path_get_dirname(initial.c_str());
    browse(parent);
    g_free(parent);
    pending_preview = initial;
  } else
    browse(initial);
  return root;
}
} // namespace kalwer::files
