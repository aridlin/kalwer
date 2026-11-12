#pragma once
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>
namespace kalwer::services {
inline constexpr const char *meme_base = "https://memes.aridlin.pl/";
inline constexpr const char *bin_base = "https://bin.aridlin.pl";
inline std::string quote(const std::string &text) {
  auto node = json_node_new(JSON_NODE_VALUE);
  json_node_set_string(node, text.c_str());
  auto raw = json_to_string(node, FALSE);
  std::string out = raw;
  g_free(raw);
  json_node_free(node);
  return out;
}
inline std::string escape(const std::string &text) {
  auto raw = g_uri_escape_string(text.c_str(), nullptr, FALSE);
  std::string out = raw;
  g_free(raw);
  return out;
}
struct Json {
  JsonParser *parser;
  explicit Json(const std::string &text) {
    parser = json_parser_new();
    GError *error = nullptr;
    if (!json_parser_load_from_data(parser, text.c_str(), text.size(),
                                    &error)) {
      std::string message = "The server returned an invalid response.";
      g_clear_error(&error);
      g_object_unref(parser);
      throw std::runtime_error(message);
    }
  }
  ~Json() { g_object_unref(parser); }
  JsonNode *root() const { return json_parser_get_root(parser); }
  JsonObject *object() const {
    if (!JSON_NODE_HOLDS_OBJECT(root()))
      throw std::runtime_error("The server returned an invalid response.");
    return json_node_get_object(root());
  }
};
inline std::string field(JsonObject *object, const char *name) {
  return json_object_has_member(object, name) &&
                 !JSON_NODE_HOLDS_NULL(json_object_get_member(object, name))
             ? json_object_get_string_member(object, name)
             : "";
}
inline std::string safe_media(const std::string &base,
                              const std::string &path) {
  if ((base == meme_base && path.starts_with(base)) ||
      (base == bin_base && path.starts_with(base + "/file/")))
    return path;
  if (base == meme_base &&
      (path.starts_with("storage/") || path.starts_with("kalwer.php?")) &&
      path.find("..") == std::string::npos)
    return base + path;
  if (base == bin_base && path.starts_with("/file/") &&
      path.find("..") == std::string::npos)
    return base + path;
  throw std::runtime_error("The server returned an invalid media address.");
}
struct Http {
  CURL *curl;
  GCancellable *cancel = nullptr;
  struct Buffer {
    std::string value;
    size_t limit;
  };
  Http() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    curl = curl_easy_init();
    if (!curl)
      throw std::runtime_error("Network client unavailable.");
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
  }
  ~Http() { curl_easy_cleanup(curl); }
  std::string request(const std::string &url, const std::string &method = "GET",
                      const std::string &body = {},
                      const std::vector<std::string> &headers = {},
                      curl_mime *mime = nullptr) {
    curl_easy_reset(curl);
    Buffer buffer{{}, 2 * 1024 * 1024};
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Kalwer");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(
        curl, CURLOPT_XFERINFOFUNCTION,
        +[](void *p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
          return p &&
                 g_cancellable_is_cancelled(static_cast<GCancellable *>(p));
        });
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancel);
    curl_easy_setopt(
        curl, CURLOPT_WRITEFUNCTION,
        +[](char *bytes, size_t size, size_t count, void *p) -> size_t {
          auto b = static_cast<Buffer *>(p);
          auto n = size * count;
          if (n > b->limit - b->value.size())
            return 0;
          b->value.append(bytes, n);
          return n;
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_slist *list = nullptr;
    for (const auto &value : headers)
      list = curl_slist_append(list, value.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    if (mime)
      curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    else if (method != "GET") {
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
      if (method != "DELETE") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(body.size()));
      }
    }
    auto result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(list);
    if (result != CURLE_OK)
      throw std::runtime_error(result == CURLE_ABORTED_BY_CALLBACK
                                   ? "Cancelled."
                                   : "The network request failed. Try again.");
    if (status < 200 || status >= 300) {
      std::string message = "The server rejected this request.";
      try {
        Json json(buffer.value);
        auto error = field(json.object(), "error");
        if (!error.empty())
          message = error;
      } catch (...) {
      }
      throw std::runtime_error(message);
    }
    return buffer.value;
  }
};
struct Item {
  std::string id, name, category, kind, url, thumbnail;
  guint64 modified = 0, size = 0;
};
struct Page {
  std::vector<Item> items;
  std::vector<std::string> categories;
  int next = -1;
};
inline Page memes(const std::string &query, const std::string &category,
                  int offset, GCancellable *cancel = nullptr,
                  const std::string &base = meme_base) {
  Http http;
  http.cancel = cancel;
  Json json(http.request(base + "kalwer.php?action=list&q=" + escape(query) +
                         "&cat=" + escape(category) +
                         "&offset=" + std::to_string(offset)));
  auto object = json.object();
  if (!json_object_get_boolean_member(object, "ok"))
    throw std::runtime_error(field(object, "error"));
  Page page;
  auto categories = json_object_get_array_member(object, "categories");
  for (guint i = 0; i < json_array_get_length(categories); ++i)
    page.categories.emplace_back(json_array_get_string_element(categories, i));
  auto rows = json_object_get_array_member(object, "items");
  for (guint i = 0; i < json_array_get_length(rows); ++i) {
    auto row = json_array_get_object_element(rows, i);
    Item item;
    item.id = field(row, "id");
    item.name = field(row, "name");
    item.category = field(row, "category");
    item.kind = field(row, "kind");
    item.url = safe_media(base, field(row, "url"));
    item.thumbnail = safe_media(base, field(row, "thumbnail"));
    item.modified = json_object_get_int_member(row, "mtime");
    page.items.push_back(std::move(item));
  }
  auto next = json_object_get_member(object, "next");
  if (next && !JSON_NODE_HOLDS_NULL(next))
    page.next = json_node_get_int(next);
  return page;
}
inline std::string cache_root() {
  auto path = std::string(g_get_user_cache_dir()) + "/kalwer/service-media";
  g_mkdir_with_parents(path.c_str(), 0700);
  return path;
}
inline std::string cached_name(const std::string &url,
                               const std::string &name) {
  auto hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, url.c_str(), -1);
  auto ext = std::filesystem::path(name).extension().string();
  if (ext.size() > 10 || !std::regex_match(ext, std::regex("\\.[a-zA-Z0-9]+")))
    ext = ".bin";
  std::string path = cache_root() + "/" + hash + ext;
  g_free(hash);
  return path;
}
inline std::string download(const std::string &url, const std::string &name,
                            guint64 limit = 200 * 1024 * 1024,
                            GCancellable *cancel = nullptr) {
  if (!(url.starts_with(meme_base) ||
        url.starts_with(std::string(bin_base) + "/file/")))
    throw std::runtime_error("Invalid media address.");
  auto path = cached_name(url, name);
  if (g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR))
    return path;
  auto tmp = path + ".tmp-XXXXXX";
  std::vector<char> chars(tmp.begin(), tmp.end());
  chars.push_back(0);
  int fd = g_mkstemp(chars.data());
  if (fd < 0)
    throw std::runtime_error("Could not create the media cache.");
  FILE *file = fdopen(fd, "wb");
  Http http;
  curl_easy_reset(http.curl);
  curl_easy_setopt(http.curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(http.curl, CURLOPT_CONNECTTIMEOUT, 8L);
  curl_easy_setopt(http.curl, CURLOPT_TIMEOUT, 300L);
  curl_easy_setopt(http.curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(http.curl, CURLOPT_MAXFILESIZE_LARGE,
                   static_cast<curl_off_t>(limit));
  struct Output {
    FILE *file;
    guint64 bytes, limit;
  } output{file, 0, limit};
  curl_easy_setopt(
      http.curl, CURLOPT_WRITEFUNCTION,
      +[](char *data, size_t s, size_t n, void *p) -> size_t {
        auto out = static_cast<Output *>(p);
        size_t bytes = s * n;
        if (bytes > out->limit - out->bytes)
          return 0;
        auto written = fwrite(data, 1, bytes, out->file);
        out->bytes += written;
        return written;
      });
  curl_easy_setopt(http.curl, CURLOPT_WRITEDATA, &output);
  curl_easy_setopt(http.curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(
      http.curl, CURLOPT_XFERINFOFUNCTION,
      +[](void *p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
        return p && g_cancellable_is_cancelled(static_cast<GCancellable *>(p));
      });
  curl_easy_setopt(http.curl, CURLOPT_XFERINFODATA, cancel);
  auto result = curl_easy_perform(http.curl);
  long status = 0;
  curl_easy_getinfo(http.curl, CURLINFO_RESPONSE_CODE, &status);
  fclose(file);
  if (result != CURLE_OK || status != 200) {
    g_unlink(chars.data());
    throw std::runtime_error("Could not download this file.");
  }
  if (g_rename(chars.data(), path.c_str()) != 0) {
    g_unlink(chars.data());
    throw std::runtime_error("Could not save the media cache.");
  }
  return path;
}
inline std::string key_path() {
  return std::string(g_get_user_config_dir()) + "/kalwer/memes-upload.key";
}
inline std::string upload_key() {
  gchar *raw = nullptr;
  gsize n = 0;
  if (!g_file_get_contents(key_path().c_str(), &raw, &n, nullptr))
    return {};
  std::string key = g_strstrip(raw);
  g_free(raw);
  return std::regex_match(key, std::regex("[a-f0-9]{64}")) ? key
                                                           : std::string{};
}
inline std::string upload_meme(const std::string &path,
                               const std::string &category,
                               const std::string &name,
                               GCancellable *cancel = nullptr,
                               bool validate = false) {
  auto key = upload_key();
  if (key.empty())
    throw std::runtime_error("Configure your upload key first.");
  if (std::filesystem::file_size(path) > 200ULL * 1024 * 1024)
    throw std::runtime_error("Memes uploads are limited to 200 MiB.");
  Http http;
  http.cancel = cancel;
  auto mime = curl_mime_init(http.curl);
  auto add = [&](const char *field, const std::string &value) {
    auto part = curl_mime_addpart(mime);
    curl_mime_name(part, field);
    curl_mime_data(part, value.c_str(), CURL_ZERO_TERMINATED);
  };
  add("category", category);
  add("name", name);
  if (validate)
    add("validate_only", "1");
  auto part = curl_mime_addpart(mime);
  curl_mime_name(part, "file");
  curl_mime_filedata(part, path.c_str());
  try {
    Json json(http.request(std::string(meme_base) + "kalwer.php?action=upload",
                           "POST", {}, {"X-Kalwer-Upload-Key: " + key}, mime));
    curl_mime_free(mime);
    auto object = json.object();
    if (validate)
      return json_object_get_boolean_member(object, "validated") ? "validated"
                                                                 : "";
    return safe_media(meme_base, field(object, "url"));
  } catch (...) {
    curl_mime_free(mime);
    throw;
  }
}
inline Page bin_items(GCancellable *cancel = nullptr) {
  Http http;
  http.cancel = cancel;
  Json json(http.request(std::string(bin_base) + "/items"));
  if (!JSON_NODE_HOLDS_ARRAY(json.root()))
    throw std::runtime_error("Invalid paste response.");
  Page page;
  auto rows = json_node_get_array(json.root());
  for (guint i = 0; i < json_array_get_length(rows); ++i) {
    auto row = json_array_get_object_element(rows, i);
    Item item;
    item.name = field(row, "name");
    item.id = std::to_string(i);
    item.size = json_object_get_int_member(row, "size");
    auto mime = field(row, "mime");
    item.kind = mime.starts_with("image/")   ? "image"
                : mime.starts_with("video/") ? "video"
                : mime.starts_with("text/")  ? "text"
                                             : "file";
    item.url = safe_media(bin_base, field(row, "url"));
    if (item.kind == "image" && item.size < 8 * 1024 * 1024)
      item.thumbnail = item.url;
    page.items.push_back(std::move(item));
  }
  return page;
}
inline std::vector<std::string>
upload_bin(const std::vector<std::string> &paths,
           GCancellable *cancel = nullptr) {
  if (paths.empty() || paths.size() > 16)
    throw std::runtime_error("Choose between one and 16 files.");
  Http http;
  http.cancel = cancel;
  auto html = http.request(std::string(bin_base) + "/");
  auto value = [&](const std::string &name) {
    std::smatch match;
    std::regex pattern("name=\"" + name + "\" value=\"([^\"]*)\"");
    return std::regex_search(html, match, pattern) ? match[1].str()
                                                   : std::string{};
  };
  auto csrf = value("csrf"), revision = value("revision");
  if (csrf.size() != 64)
    throw std::runtime_error("Could not start an upload session.");
  std::string body =
      "{\"action\":" + quote(revision.empty() ? "replace" : "append") +
      ",\"revision\":" + quote(revision) + ",\"items\":[";
  guint64 total = 0;
  for (size_t i = 0; i < paths.size(); ++i) {
    auto size = std::filesystem::file_size(paths[i]);
    total += size;
    if (size > 1024ULL * 1024 * 1024 || total > 2ULL * 1024 * 1024 * 1024)
      throw std::runtime_error(
          "Binup allows 1 GiB per file and 2 GiB per paste.");
    if (i)
      body += ",";
    body += "{\"name\":" +
            quote(std::filesystem::path(paths[i]).filename().string()) +
            ",\"mime\":\"application/octet-stream\",\"size\":" +
            std::to_string(size) + "}";
  }
  body += "]}";
  auto headers = std::vector<std::string>{"X-CSRF-Token: " + csrf,
                                          "Content-Type: application/json"};
  Json start(
      http.request(std::string(bin_base) + "/uploads", "POST", body, headers));
  auto id = field(start.object(), "id");
  if (!std::regex_match(id, std::regex("[a-f0-9]{48}")))
    throw std::runtime_error("Invalid upload session.");
  auto base = std::string(bin_base) + "/uploads/" + id;
  try {
    std::vector<char> chunk(4 * 1024 * 1024);
    for (size_t i = 0; i < paths.size(); ++i) {
      std::ifstream input(paths[i], std::ios::binary);
      guint64 offset = 0;
      while (input) {
        input.read(chunk.data(), chunk.size());
        auto count = input.gcount();
        if (count <= 0)
          break;
        std::string data(chunk.data(), count);
        std::string reply;
        for (int retry = 0;; ++retry)
          try {
            reply = http.request(base + "/" + std::to_string(i) +
                                     "?offset=" + std::to_string(offset),
                                 "PUT", data,
                                 {"X-CSRF-Token: " + csrf,
                                  "Content-Type: application/octet-stream"});
            break;
          } catch (...) {
            if (retry >= 1 || (cancel && g_cancellable_is_cancelled(cancel)))
              throw;
          }
        Json result(reply);
        offset += count;
        if (json_object_get_int_member(result.object(), "offset") !=
            static_cast<gint64>(offset))
          throw std::runtime_error("The server did not accept this chunk.");
      }
    }
    Json committed(
        http.request(base + "/commit", "POST", "", {"X-CSRF-Token: " + csrf}));
    auto rev = field(committed.object(), "revision");
    auto result = bin_items(cancel);
    std::vector<std::string> urls;
    for (auto &item : result.items)
      if (item.url.find("revision=" + rev) != std::string::npos)
        urls.push_back(item.url);
    if (urls.size() >= paths.size())
      urls.erase(urls.begin(), urls.end() - paths.size());
    if (urls.empty())
      urls.push_back(std::string(bin_base) + "/");
    return urls;
  } catch (...) {
    http.cancel = nullptr;
    try {
      http.request(base, "DELETE", "", {"X-CSRF-Token: " + csrf});
    } catch (...) {
    }
    throw;
  }
}
} // namespace kalwer::services
