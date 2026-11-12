#include "../service_client.hpp"
#include <cassert>
#include <iostream>
int main() {
  using namespace kalwer::services;
  for (auto url :
       {"https://bin.aridlin.pl.evil/file/test",
        "http://bin.aridlin.pl/file/test", "https://evil.test/file/test"}) {
    bool rejected = false;
    try {
      safe_media(bin_base, url);
    } catch (...) {
      rejected = true;
    }
    assert(rejected);
  }
  auto page = memes("", "all", 0);
  assert(!page.categories.empty());
  assert(!page.items.empty());
  assert(page.items.size() <= 48);
  auto path =
      download(page.items.front().thumbnail, "thumbnail.jpg", 8 * 1024 * 1024);
  assert(std::filesystem::file_size(path) > 0);
  auto bin = bin_items();
  std::cout << "Memes catalog, lazy thumbnail and Binup catalog passed ("
            << page.items.size() << ", " << bin.items.size() << ").\n";
  Http http;
  auto html = http.request(std::string(bin_base) + "/");
  std::smatch match;
  assert(std::regex_search(html, match,
                           std::regex("name=\"csrf\" value=\"([^\"]*)\"")));
  auto csrf = match[1].str();
  std::string body =
      "{\"action\":\"replace\",\"revision\":\"\",\"items\":[{\"name\":\"kalwer-"
      "protocol-test.txt\",\"mime\":\"text/plain\",\"size\":4}]}";
  Json start(http.request(
      std::string(bin_base) + "/uploads", "POST", body,
      {"X-CSRF-Token: " + csrf, "Content-Type: application/json"}));
  auto id = field(start.object(), "id");
  assert(std::regex_match(id, std::regex("[a-f0-9]{48}")));
  auto session = std::string(bin_base) + "/uploads/" + id;
  for (int i = 0; i < 2; i++) {
    Json result(http.request(
        session + "/0?offset=0", "PUT", "test",
        {"X-CSRF-Token: " + csrf, "Content-Type: application/octet-stream"}));
    assert(json_object_get_int_member(result.object(), "offset") == 4);
  }
  http.request(session, "DELETE", {}, {"X-CSRF-Token: " + csrf});
  std::cout << "Binup session, chunk retry and cancellation passed; no live "
               "paste committed.\n";
}
