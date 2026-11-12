#pragma once
#include "file_browser_gtk.hpp"
#include <map>
#include <memory>
namespace kalwer::services {
struct Popup : std::enable_shared_from_this<Popup> {
  GtkWidget *host, *root, *controls, *category = nullptr, *upload_box = nullptr;
  bool meme, alive = true, busy = false, choosing = false, pending_open = false;
  unsigned serial = 0;
  int next = -1;
  std::string query, cat = "all";
  std::map<std::string, Item> records;
  std::vector<std::string> categories;
  GCancellable *cancel = g_cancellable_new();
  std::function<void()> close;
  Popup(GtkWidget *w, bool m, std::function<void()> c)
      : host(w), meme(m), close(std::move(c)) {}
  ~Popup() { g_object_unref(cancel); }
  void status(const std::string &text) {
    gtk_label_set_text(GTK_LABEL(files::status), text.c_str());
  }
  struct Work {
    std::shared_ptr<Popup> session;
    unsigned serial;
    std::function<void()> work, done;
    std::string error;
  };
  void async(std::function<void()> work, std::function<void()> done) {
    auto job = new Work{
        shared_from_this(), serial, std::move(work), std::move(done), {}};
    auto task = g_task_new(
        nullptr, cancel,
        +[](GObject *, GAsyncResult *result, gpointer) {
          auto w = static_cast<Work *>(g_task_get_task_data(G_TASK(result)));
          if (!w->session->alive || w->serial != w->session->serial)
            return;
          w->session->busy = false;
          if (!w->error.empty())
            w->session->status(w->error);
          else
            w->done();
        },
        nullptr);
    g_task_set_task_data(
        task, job, +[](gpointer p) { delete static_cast<Work *>(p); });
    g_task_run_in_thread(
        task, +[](GTask *t, gpointer, gpointer data, GCancellable *) {
          auto w = static_cast<Work *>(data);
          try {
            w->work();
          } catch (const std::exception &e) {
            w->error = e.what();
          }
          g_task_return_boolean(t, TRUE);
        });
    g_object_unref(task);
  }
  void bind() {
    auto weak = weak_from_this();
    files::service_mode = true;
    gtk_widget_hide(files::places);
    gtk_widget_set_no_show_all(files::places, TRUE);
    if (category)
      gtk_widget_show(category);
    gtk_widget_hide(files::toolbar);
    gtk_widget_set_no_show_all(files::toolbar, TRUE);
    gtk_widget_show(controls);
    files::external_open = [weak](const auto &path) {
      if (auto p = weak.lock())
        p->open(path, false);
    };
    files::external_copy = [weak](const auto &path) {
      if (auto p = weak.lock())
        p->open(path, true);
    };
    files::external_search = [weak](const auto &text) {
      if (auto p = weak.lock()) {
        if (p->query == text)
          return;
        p->query = text;
        p->fetch(false);
      }
    };
    files::external_activate = [weak] {
      if (auto p = weak.lock()) {
        auto text = std::string(gtk_entry_get_text(GTK_ENTRY(files::search)));
        if (text != p->query) {
          p->query = text;
          p->pending_open = true;
          p->fetch(false);
        } else if (p->busy)
          p->pending_open = true;
        else
          files::first_match(true);
      }
    };
    files::external_more = [weak] {
      if (auto p = weak.lock()) {
        auto a = gtk_scrolled_window_get_vadjustment(
            GTK_SCROLLED_WINDOW(files::scroller));
        if (p->next >= 0 && !p->busy &&
            gtk_adjustment_get_upper(a) - gtk_adjustment_get_value(a) -
                    gtk_adjustment_get_page_size(a) <
                gtk_adjustment_get_page_size(a))
          p->fetch(true);
      }
    };
    files::external_escape = [weak] {
      if (auto p = weak.lock()) {
        if (p->choosing) {
          p->restore();
          return true;
        }
      }
      return false;
    };
  }
  files::Entry entry(const Item &item) {
    auto path = cached_name(item.url, item.name);
    records[path] = item;
    auto folded = g_utf8_casefold(item.name.c_str(), -1);
    files::Entry e{path,
                   item.name,
                   item.kind == "image"   ? "image-x-generic"
                   : item.kind == "video" ? "video-x-generic"
                                          : "text-x-generic",
                   item.kind == "image" ? item.thumbnail : "",
                   false,
                   item.modified,
                   folded};
    g_free(folded);
    return e;
  }
  void fetch(bool more) {
    if (!alive || choosing || (more && busy))
      return;
    if (!more) {
      ++serial;
      g_cancellable_cancel(cancel);
      g_object_unref(cancel);
      cancel = g_cancellable_new();
      files::entries.clear();
      records.clear();
      files::refill();
      next = -1;
    }
    busy = true;
    status("Loading…");
    auto page = std::make_shared<Page>();
    auto q = query, c = cat;
    int offset = more ? next : 0;
    auto token = cancel;
    g_object_ref(token);
    async(
        [page, q, c, offset, token, m = meme] {
          try {
            *page = m ? memes(q, c, offset, token) : bin_items(token);
          } catch (...) {
            g_object_unref(token);
            throw;
          }
          g_object_unref(token);
        },
        [self = shared_from_this(), page, more] {
          self->next = page->next;
          if (self->meme && self->categories != page->categories) {
            self->categories = page->categories;
            g_signal_handlers_block_matched(self->category, G_SIGNAL_MATCH_DATA,
                                            0, 0, nullptr, nullptr, self.get());
            gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(self->category));
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(self->category), "all",
                                      "All categories");
            for (const auto &c : page->categories)
              gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(self->category),
                                        c.c_str(), c.c_str());
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(self->category),
                                        self->cat.c_str());
            g_signal_handlers_unblock_matched(self->category,
                                              G_SIGNAL_MATCH_DATA, 0, 0,
                                              nullptr, nullptr, self.get());
          }
          size_t first = files::entries.size();
          for (const auto &item : page->items)
            files::entries.push_back(self->entry(item));
          if (more) {
            for (size_t i = first; i < files::entries.size(); ++i)
              files::matches.push_back(i);
            files::append_page();
          } else
            files::refill();
          self->status(files::entries.empty() ? "No matching items." : "");
          if (self->pending_open) {
            self->pending_open = false;
            files::first_match(true);
          }
        });
  }
  void enable_drag(const std::string &path) {
    auto children = gtk_container_get_children(GTK_CONTAINER(files::grid));
    for (auto l = children; l; l = l->next) {
      auto value = static_cast<const char *>(
          g_object_get_data(G_OBJECT(l->data), "path"));
      if (value && path == value) {
        auto tile = gtk_bin_get_child(GTK_BIN(l->data));
        if (!g_object_get_data(G_OBJECT(tile), "drag-enabled"))
          files::drag(tile, path);
      }
    }
    g_list_free(children);
  }
  void open(const std::string &path, bool copy) {
    if (path.empty())
      return;
    if (g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR)) {
      if (copy)
        files::copy_path(path);
      else
        files::show_file(path);
      return;
    }
    auto found = records.find(path);
    if (found == records.end())
      return;
    auto item = found->second;
    if (busy)
      return;
    busy = true;
    status("Downloading…");
    auto result = std::make_shared<std::string>();
    auto token = cancel;
    g_object_ref(token);
    async(
        [result, item, token] {
          try {
            *result =
                download(item.url, item.name, 1024ULL * 1024 * 1024, token);
          } catch (...) {
            g_object_unref(token);
            throw;
          }
          g_object_unref(token);
        },
        [self = shared_from_this(), result, copy] {
          self->status("");
          self->enable_drag(*result);
          if (copy)
            files::copy_path(*result);
          else
            files::show_file(*result);
        });
  }
  void key_dialog() {
    auto dialog = gtk_dialog_new_with_buttons(
        "Memes upload key", GTK_WINDOW(host), GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, nullptr);
    auto box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    auto input = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(input), FALSE);
    gtk_entry_set_text(GTK_ENTRY(input), upload_key().c_str());
    gtk_entry_set_placeholder_text(GTK_ENTRY(input), "Private upload key");
    gtk_box_pack_start(GTK_BOX(box), input, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(dialog), "key", input);
    g_signal_connect(
        dialog, "response",
        G_CALLBACK(+[](GtkDialog *dialog, int response, gpointer) {
          if (response == GTK_RESPONSE_ACCEPT) {
            auto input = GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "key"));
            auto key = std::string(gtk_entry_get_text(input));
            if (!std::regex_match(key, std::regex("[a-f0-9]{64}")))
              return;
            auto path = key_path();
            auto parent = g_path_get_dirname(path.c_str());
            g_mkdir_with_parents(parent, 0700);
            g_free(parent);
            g_file_set_contents(path.c_str(), key.c_str(), key.size(), nullptr);
            g_chmod(path.c_str(), 0600);
          }
          gtk_widget_destroy(GTK_WIDGET(dialog));
        }),
        nullptr);
    gtk_widget_show_all(dialog);
  }
  void choose() {
    if (busy)
      return;
    choosing = true;
    ++serial;
    files::external_more = {};
    files::external_search = {};
    files::external_activate = {};
    files::external_copy = {};
    files::service_mode = false;
    gtk_widget_set_no_show_all(files::places, FALSE);
    gtk_widget_show(files::places);
    if (category)
      gtk_widget_hide(category);
    gtk_widget_hide(controls);
    gtk_widget_set_no_show_all(files::toolbar, FALSE);
    gtk_widget_show(files::toolbar);
    auto weak = weak_from_this();
    files::external_open = [weak](const std::string &path) {
      if (auto self = weak.lock()) {
        if (g_file_test(path.c_str(), G_FILE_TEST_IS_DIR))
          files::browse(path);
        else
          self->upload_form(path);
      }
    };
    gtk_entry_set_text(GTK_ENTRY(files::search), "");
    files::browse("");
  }
  void restore() {
    ++files::generation;
    if (files::listing_cancel)
      g_cancellable_cancel(files::listing_cancel);
    files::pending_preview.clear();
    choosing = false;
    files::previewing = false;
    if (upload_box) {
      gtk_widget_destroy(upload_box);
      upload_box = nullptr;
    }
    bind();
    gtk_widget_show(files::browser_tools);
    gtk_stack_set_visible_child_name(GTK_STACK(files::stack), "files");
    auto text = query;
    gtk_entry_set_text(GTK_ENTRY(files::search), text.c_str());
    fetch(false);
  }
  void upload_form(const std::string &path) {
    if (upload_box)
      gtk_widget_destroy(upload_box);
    upload_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_stack_add_named(GTK_STACK(files::stack), upload_box, "upload");
    auto label =
        gtk_label_new(std::filesystem::path(path).filename().string().c_str());
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(upload_box), label, FALSE, FALSE, 4);
    auto name = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(name),
                       std::filesystem::path(path).stem().string().c_str());
    gtk_box_pack_start(GTK_BOX(upload_box), name, FALSE, FALSE, 4);
    auto choice = gtk_combo_box_text_new();
    if (meme) {
      for (const auto &c : categories)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(choice), c.c_str(),
                                  c.c_str());
      gtk_combo_box_set_active(GTK_COMBO_BOX(choice), 0);
      if (cat != "all")
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(choice), cat.c_str());
      gtk_box_pack_start(GTK_BOX(upload_box), choice, FALSE, FALSE, 0);
    }
    auto actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(upload_box), actions, FALSE, FALSE, 4);
    auto send = gtk_button_new_with_label("Upload");
    auto back = gtk_button_new_with_label("Cancel");
    gtk_box_pack_start(GTK_BOX(actions), send, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(actions), back, TRUE, TRUE, 0);
    struct Upload {
      std::shared_ptr<Popup> self;
      std::string path;
      GtkWidget *name;
      GtkWidget *category;
    };
    auto data = new Upload{shared_from_this(), path, name, choice};
    g_object_set_data_full(
        G_OBJECT(send), "upload", data,
        +[](gpointer p) { delete static_cast<Upload *>(p); });
    g_signal_connect(
        send, "clicked", G_CALLBACK(+[](GtkWidget *button, gpointer) {
          auto w = static_cast<Upload *>(
              g_object_get_data(G_OBJECT(button), "upload"));
          if (w->self->busy)
            return;
          std::string category;
          auto id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(w->category));
          if (id)
            category = id;
          auto name = std::string(gtk_entry_get_text(GTK_ENTRY(w->name)));
          w->self->send(w->path, category, name);
        }),
        nullptr);
    g_signal_connect_swapped(back, "clicked",
                             G_CALLBACK(+[](Popup *p) { p->restore(); }), this);
    gtk_widget_hide(files::toolbar);
    gtk_widget_hide(files::browser_tools);
    gtk_widget_show_all(upload_box);
    gtk_stack_set_visible_child_name(GTK_STACK(files::stack), "upload");
  }
  void send(const std::string &path, const std::string &category,
            const std::string &name) {
    if (busy)
      return;
    if (meme && upload_key().empty()) {
      key_dialog();
      return;
    }
    busy = true;
    status("Uploading…");
    auto result = std::make_shared<std::string>();
    auto token = cancel;
    g_object_ref(token);
    async(
        [m = meme, result, path, category, name, token] {
          try {
            if (m)
              *result = upload_meme(path, category, name, token);
            else {
              auto urls = upload_bin({path}, token);
              for (auto &url : urls) {
                if (!result->empty())
                  *result += '\n';
                *result += url;
              }
            }
          } catch (...) {
            g_object_unref(token);
            throw;
          }
          g_object_unref(token);
        },
        [self = shared_from_this(), result] {
          gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                                 result->c_str(), result->size());
          self->restore();
          self->status("Uploaded. Link copied.");
        });
  }
  void text_form() {
    if (busy)
      return;
    choosing = true;
    files::external_more = {};
    files::external_search = {};
    files::external_activate = {};
    upload_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_stack_add_named(GTK_STACK(files::stack), upload_box, "text");
    auto input = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(input), GTK_WRAP_WORD_CHAR);
    auto scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scroll), input);
    gtk_box_pack_start(GTK_BOX(upload_box), scroll, TRUE, TRUE, 0);
    auto button = gtk_button_new_with_label("Continue");
    gtk_box_pack_start(GTK_BOX(upload_box), button, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(button), "input", input);
    g_signal_connect(
        button, "clicked", G_CALLBACK(+[](GtkWidget *button, gpointer data) {
          auto self = static_cast<Popup *>(data);
          auto view =
              GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(button), "input"));
          auto buffer = gtk_text_view_get_buffer(view);
          GtkTextIter a;
          GtkTextIter b;
          gtk_text_buffer_get_bounds(buffer, &a, &b);
          auto text = gtk_text_buffer_get_text(buffer, &a, &b, FALSE);
          auto path = cache_root() + "/text-" +
                      std::to_string(g_get_monotonic_time()) + ".txt";
          if (*text)
            g_file_set_contents(path.c_str(), text, -1, nullptr);
          g_free(text);
          self->upload_form(path);
        }),
        this);
    gtk_widget_hide(files::toolbar);
    gtk_widget_hide(files::browser_tools);
    gtk_widget_show_all(upload_box);
    gtk_stack_set_visible_child_name(GTK_STACK(files::stack), "text");
  }
};
inline GtkWidget *create(GtkWidget *host, bool meme,
                         std::function<void()> close) {
  auto session = std::make_shared<Popup>(host, meme, std::move(close));
  session->root = files::create(host, "", session->close, false);
  session->controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  auto title = gtk_label_new(meme ? "Memes" : "Binup");
  gtk_box_pack_start(GTK_BOX(session->controls), title, TRUE, TRUE, 0);
  auto action = [&](const char *label, GCallback callback) {
    auto b = gtk_button_new_with_label(label);
    gtk_box_pack_start(GTK_BOX(session->controls), b, FALSE, FALSE, 0);
    g_signal_connect_swapped(b, "clicked", callback, session.get());
  };
  action("Upload", G_CALLBACK(+[](Popup *p) { p->choose(); }));
  action("Text", G_CALLBACK(+[](Popup *p) { p->text_form(); }));
  if (meme)
    action("Key", G_CALLBACK(+[](Popup *p) { p->key_dialog(); }));
  action("↻", G_CALLBACK(+[](Popup *p) { p->fetch(false); }));
  action("×", G_CALLBACK(+[](Popup *p) { p->close(); }));
  gtk_box_pack_start(GTK_BOX(session->root), session->controls, FALSE, FALSE,
                     0);
  gtk_box_reorder_child(GTK_BOX(session->root), session->controls, 0);
  files::clear(files::places);
  if (meme) {
    session->category = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(session->category), "all",
                              "All categories");
    gtk_combo_box_set_active(GTK_COMBO_BOX(session->category), 0);
    gtk_box_pack_start(GTK_BOX(session->root), session->category, FALSE, FALSE,
                       0);
    gtk_box_reorder_child(GTK_BOX(session->root), session->category, 1);
    g_signal_connect(session->category, "changed",
                     G_CALLBACK(+[](GtkComboBox *combo, gpointer data) {
                       auto p = static_cast<Popup *>(data);
                       auto id = gtk_combo_box_get_active_id(combo);
                       p->cat = id ? id : "all";
                       p->fetch(false);
                     }),
                     session.get());
  }
  g_object_set_data_full(
      G_OBJECT(host), "kalwer-service-popup",
      new std::shared_ptr<Popup>(session), +[](gpointer p) {
        auto ref = static_cast<std::shared_ptr<Popup> *>(p);
        (*ref)->alive = false;
        g_cancellable_cancel((*ref)->cancel);
        delete ref;
      });
  g_signal_connect(
      host, "destroy", G_CALLBACK(+[](GtkWidget *host, gpointer) {
        auto p = static_cast<std::shared_ptr<Popup> *>(
            g_object_get_data(G_OBJECT(host), "kalwer-service-popup"));
        if (p) {
          (*p)->alive = false;
          g_cancellable_cancel((*p)->cancel);
        }
      }),
      nullptr);
  session->bind();
  session->fetch(false);
  return session->root;
}
} // namespace kalwer::services
