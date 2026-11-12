#include "../windows/categories.hpp"
#include "windows_image_fixtures.hpp"
#include <bcrypt.h>
#include <cassert>
#include <iostream>
#include <windows.h>
void wait(const std::shared_ptr<kalwer::winfiles::Panel> &p) {
  for (int i = 0; i < 3000 && p->busy; i++) {
    p->poll();
    Sleep(10);
  }
  p->poll();
  assert(!p->busy);
}
int main() {
  using namespace kalwer::winfiles;
  assert(SUCCEEDED(OleInitialize(nullptr)));
  auto root =
      fs::temp_directory_path() /
      (L"kalwer-feature-fixtures-" + std::to_wstring(GetCurrentProcessId()));
  assert(!fs::exists(root));
  assert(!fs::exists(kalwer::winservices::data() / L"app-groups.json"));
  assert(!fs::exists(kalwer::winservices::data() / L"file-bookmarks.json"));
  fs::create_directories(root / L"Downloads" / L"older-folder");
  assert(downloads(known(FOLDERID_Downloads)));
  auto file = root / L"Downloads" / L"new-żółć.txt";
  {
    std::ofstream out(file);
    out << "Preview fixture";
  }
  auto now = fs::file_time_type::clock::now();
  fs::last_write_time(file, now);
  {
    auto folder = root / L"Downloads" / L"older-folder";
    auto handle = CreateFileW(
        folder.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    assert(handle != INVALID_HANDLE_VALUE);
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    value.QuadPart -= 864000000000ULL;
    time.dwLowDateTime = value.LowPart;
    time.dwHighDateTime = value.HighPart;
    assert(SetFileTime(handle, nullptr, nullptr, &time));
    CloseHandle(handle);
  }
  auto png = root / L"sample.png";
  {
    std::ofstream out(png, std::ios::binary);
    out.write(reinterpret_cast<const char *>(kalwer::test::png),
              sizeof(kalwer::test::png));
  }
  auto bitmap = image_thumbnail(png, 160);
  assert(bitmap && bitmap->handle);
  BITMAP info{};
  assert(GetObjectW(bitmap->handle, sizeof(info), &info));
  assert(info.bmWidth == 16 && info.bmHeight == 8);
  assert(info.bmBits &&
         ((*static_cast<DWORD *>(info.bmBits)) & 0x00ffffff) == 0x00ff0000);
  auto gif = root / L"sample.gif";
  {
    std::ofstream out(gif, std::ios::binary);
    out.write(reinterpret_cast<const char *>(kalwer::test::gif),
              sizeof(kalwer::test::gif));
  }
  {
    Gif animation(gif);
    for (int i = 0; i < 200; i++) {
      bool ready;
      {
        std::lock_guard lock(animation.data->mutex);
        ready = animation.data->revision > 0;
      }
      if (ready)
        break;
      Sleep(10);
    }
    unsigned first;
    {
      std::lock_guard lock(animation.data->mutex);
      first = animation.data->revision;
      assert(first > 0 && animation.data->frame);
    }
    animation.data->paused = false;
    for (int i = 0; i < 200; i++) {
      bool advanced;
      {
        std::lock_guard lock(animation.data->mutex);
        advanced = animation.data->revision > first;
      }
      if (advanced)
        break;
      Sleep(10);
    }
    {
      std::lock_guard lock(animation.data->mutex);
      assert(animation.data->revision > first);
    }
  }
  auto panel = std::make_shared<Panel>();
  panel->load_places();
  panel->browse(root / L"Downloads");
  wait(panel);
  assert(panel->directory == root / L"Downloads");
  assert(panel->entries.size() == 2);
  assert(panel->entries.front().folder);
  sort_entries(panel->entries, true);
  assert(panel->entries.front().name == L"new-żółć.txt");
  assert(panel->entries.back().folder);
  panel->query = "ŻÓŁĆ";
  panel->filter();
  assert(panel->visible.size() == 1);
  panel->query = "new";
  panel->filter();
  assert(panel->visible.size() == 1);
  panel->open_index();
  assert(panel->preview && panel->preview_text == L"Preview fixture");
  panel->copy_selected();
  IDataObject *object = nullptr;
  assert(SUCCEEDED(OleGetClipboard(&object)));
  FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM medium{};
  assert(SUCCEEDED(object->GetData(&format, &medium)));
  auto drop = reinterpret_cast<HDROP>(medium.hGlobal);
  assert(DragQueryFileW(drop, 0xffffffff, nullptr, 0) == 1);
  wchar_t dropped[32768];
  DragQueryFileW(drop, 0, dropped, 32768);
  assert(fs::path(dropped) == file);
  ReleaseStgMedium(&medium);
  object->Release();
  panel->back();
  assert(!panel->preview);
  panel->bookmarks.push_back(root);
  panel->save_places();
  auto saved = std::make_shared<Panel>();
  saved->load_places();
  assert(saved->bookmarks.back() == root);
  panel->shutdown();
  using namespace kalwer::wingroups;
  std::vector<App> apps = {{L"firefox", L"Firefox", L"Internet"},
                           {L"steam", L"Steam", L"Games"},
                           {L"editor", L"Code", L"Development"}};
  Group group{L"internet", true, {}};
  assert(members(group, apps).size() == 1);
  group.order = {L"steam", L"firefox"};
  assert(members(group, apps).size() == 2 &&
         members(group, apps)[0].id == L"steam");
  group.automatic = false;
  group.order = {L"editor"};
  assert(members(group, apps)[0].id == L"editor");
  save({group});
  assert(load()[0].order[0] == L"editor");
  for (auto invalid :
       {"https://bin.aridlin.pl.evil/file/test",
        "http://bin.aridlin.pl/file/test", "https://evil.test/file/test"}) {
    bool rejected = false;
    try {
      kalwer::winservices::media(invalid, false);
    } catch (...) {
      rejected = true;
    }
    assert(rejected);
  }
  assert(kalwer::winservices::media("storage/image/a.png", true) ==
         "https://memes.aridlin.pl/storage/image/a.png");
  auto secret = std::string(64, 'a');
  kalwer::winservices::secret(L"test-key.dpapi", secret);
  assert(kalwer::winservices::secret(L"test-key.dpapi") == secret);
  assert(
      kalwer::winservices::read(kalwer::winservices::data() / L"test-key.dpapi")
          .find(secret) == std::string::npos);
  fs::remove(kalwer::winservices::data() / L"test-key.dpapi");
  fs::create_directories(root / L"large");
  for (int i = 0; i < 5000; i++) {
    std::ofstream out(root / L"large" /
                      (L"file_" + std::to_wstring(i) + L".txt"));
    out << "fixture";
  }
  auto large = std::make_shared<Panel>();
  auto began = GetTickCount64();
  large->browse(root / L"large");
  assert(GetTickCount64() - began < 500);
  wait(large);
  assert(large->entries.size() == 5000);
  large->query = "file_4999";
  large->filter();
  assert(large->visible.size() == 1);
  large->browse(root / L"large");
  large->shutdown();
  for (int i = 0; i < 1000 && large.use_count() > 1; i++)
    Sleep(10);
  assert(large.use_count() == 1);
  fs::remove(kalwer::winservices::data() / L"app-groups.json");
  fs::remove(kalwer::winservices::data() / L"file-bookmarks.json");
  fs::remove_all(root);
  OleSetClipboard(nullptr);
  std::error_code cleanup;
  fs::remove(kalwer::winservices::data(), cleanup);
  OleUninitialize();
  std::cout << "Windows Downloads date ordering, default, filtering, preview, "
               "native file clipboard, bookmarks, group ordering and DPAPI "
               "secret roundtrip, PNG/GIF decoding, 5k-file search and "
               "cancellation passed.\n";
  return 0;
}
