#pragma once
#include "file_popup.hpp"
namespace kalwer::wingroups {
using namespace winservices;
struct App {
  std::wstring id, name, source;
};
struct Group {
  std::wstring name;
  bool automatic = false;
  std::vector<std::wstring> order;
};
inline std::vector<Group> defaults() {
  std::vector<Group> out;
  for (auto name :
       {L"games", L"internet", L"office", L"graphics", L"development",
        L"multimedia", L"system", L"utilities", L"education"})
    out.push_back({name, true, {}});
  return out;
}
inline std::vector<Group> load() {
  auto out = defaults();
  try {
    auto rows = Json::parse(read(data() / L"app-groups.json"));
    out.clear();
    for (auto &row : rows) {
      Group group;
      group.name = wide(row.at("name").get<std::string>());
      group.automatic = row.value("automatic", false);
      for (auto &id : row.at("apps"))
        group.order.push_back(wide(id.get<std::string>()));
      out.push_back(std::move(group));
    }
  } catch (...) {
  }
  return out;
}
inline void save(const std::vector<Group> &groups) {
  Json rows = Json::array();
  for (auto &g : groups) {
    Json ids = Json::array();
    for (auto &id : g.order)
      ids.push_back(utf8(id));
    rows.push_back(
        {{"name", utf8(g.name)}, {"automatic", g.automatic}, {"apps", ids}});
  }
  atomic_save(data() / L"app-groups.json", rows.dump());
}
inline bool automatic(const App &a, const std::wstring &group) {
  auto text = winfiles::lower(a.name + L" " + a.source);
  static const std::map<std::wstring, std::vector<std::wstring>> tags = {
      {L"games",
       {L"game", L"steam", L"xbox", L"gog", L"epic", L"minecraft",
        L"kalwer /"}},
      {L"internet",
       {L"browser", L"firefox", L"chrome", L"edge", L"vivaldi", L"discord",
        L"telegram", L"signal", L"thunderbird"}},
      {L"office",
       {L"office", L"word", L"excel", L"powerpoint", L"writer", L"calc",
        L"acrobat", L"onenote"}},
      {L"graphics",
       {L"paint", L"gimp", L"krita", L"inkscape", L"photoshop", L"blender",
        L"illustrator"}},
      {L"development",
       {L"visual studio", L"code", L"python", L"git", L"idea",
        L"android studio", L"terminal"}},
      {L"multimedia",
       {L"vlc", L"spotify", L"music", L"video", L"obs", L"audacity",
        L"media player"}},
      {L"system",
       {L"settings", L"control panel", L"manager", L"powershell", L"registry",
        L"services"}},
      {L"utilities",
       {L"notepad", L"7-zip", L"winrar", L"calculator", L"everything",
        L"explorer"}},
      {L"education", {L"anki", L"geogebra", L"classroom", L"education"}}};
  auto it = tags.find(group);
  return it != tags.end() &&
         std::any_of(it->second.begin(), it->second.end(),
                     [&](auto &tag) { return text.find(tag) != text.npos; });
}
inline std::vector<App> members(const Group &group,
                                const std::vector<App> &apps,
                                const std::wstring &query = {}) {
  std::vector<App> out;
  auto add = [&](const App &a) {
    if (winfiles::lower(a.name + L" " + a.id).find(winfiles::lower(query)) !=
        std::wstring::npos)
      out.push_back(a);
  };
  for (auto &id : group.order) {
    auto it = std::find_if(apps.begin(), apps.end(),
                           [&](auto &a) { return a.id == id; });
    if (it != apps.end())
      add(*it);
  }
  if (group.automatic) {
    std::vector<App> rest;
    for (auto &a : apps)
      if (automatic(a, group.name) &&
          std::find(group.order.begin(), group.order.end(), a.id) ==
              group.order.end())
        rest.push_back(a);
    std::stable_sort(rest.begin(), rest.end(), [](auto &a, auto &b) {
      return winfiles::lower(a.name) < winfiles::lower(b.name);
    });
    for (auto &a : rest)
      add(a);
  }
  return out;
}
struct Editor {
  HWND window = nullptr, list = nullptr, apps = nullptr, automatic = nullptr;
  std::vector<Group> groups;
  std::vector<App> catalog, ordered;
  int selected = 0;
  bool done = false;
  void populate() {
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (auto &g : groups)
      SendMessageW(list, LB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(g.name.c_str()));
    selected = std::clamp(selected, 0, std::max(0, int(groups.size()) - 1));
    SendMessageW(list, LB_SETCURSEL, selected, 0);
    show();
  }
  void capture() {
    if (groups.empty())
      return;
    auto &g = groups[selected];
    g.automatic = SendMessageW(automatic, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g.order.clear();
    for (size_t i = 0; i < ordered.size(); i++)
      if (SendMessageW(apps, LB_GETSEL, i, 0) > 0)
        g.order.push_back(ordered[i].id);
  }
  void show() {
    SendMessageW(apps, LB_RESETCONTENT, 0, 0);
    if (groups.empty())
      return;
    auto &g = groups[selected];
    ordered = members(g, catalog);
    for (auto &a : catalog)
      if (std::none_of(ordered.begin(), ordered.end(),
                       [&](auto &b) { return a.id == b.id; }))
        ordered.push_back(a);
    for (auto &a : ordered)
      SendMessageW(apps, LB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(a.name.c_str()));
    auto active = members(g, catalog);
    for (size_t i = 0; i < ordered.size(); i++)
      SendMessageW(apps, LB_SETSEL,
                   std::any_of(active.begin(), active.end(),
                               [&](auto &a) { return a.id == ordered[i].id; }),
                   i);
    SendMessageW(automatic, BM_SETCHECK,
                 g.automatic ? BST_CHECKED : BST_UNCHECKED, 0);
  }
};
inline void edit(HWND owner, std::vector<App> catalog) {
  static const wchar_t cls[] = L"KalwerAppGroups";
  WNDCLASSW wc{};
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = cls;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpfnWndProc =
      +[](HWND window, UINT message, WPARAM w, LPARAM l) -> LRESULT {
    auto e =
        reinterpret_cast<Editor *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CREATE) {
      e = static_cast<Editor *>(
          reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(e));
    }
    if (!e)
      return DefWindowProcW(window, message, w, l);
    if (message == WM_CLOSE) {
      e->done = true;
      return 0;
    }
    if (message == WM_COMMAND) {
      int id = LOWORD(w);
      if (id == 100 && HIWORD(w) == LBN_SELCHANGE) {
        int next = int(SendMessageW(e->list, LB_GETCURSEL, 0, 0));
        e->capture();
        e->selected = std::max(0, next);
        e->show();
      } else if (id == 104) {
        auto name = winfiles::prompt(window, L"Group name");
        if (name && !name->empty() && name->size() <= 48 &&
            name->find_first_of(L" \t\r\n&/\\") == name->npos &&
            std::none_of(e->groups.begin(), e->groups.end(),
                         [&](auto &g) { return g.name == *name; })) {
          e->capture();
          e->groups.push_back({*name, false, {}});
          e->selected = int(e->groups.size()) - 1;
          e->populate();
        }
      } else if (id == 105 && !e->groups.empty()) {
        e->groups.erase(e->groups.begin() + e->selected);
        e->populate();
      } else if (id == 106 || id == 107) {
        auto index = int(SendMessageW(e->apps, LB_GETCARETINDEX, 0, 0));
        int target = index + (id == 106 ? -1 : 1);
        if (index >= 0 && target >= 0 && target < int(e->ordered.size())) {
          e->capture();
          auto selected = e->groups[e->selected].order;
          std::swap(e->ordered[index], e->ordered[target]);
          auto &g = e->groups[e->selected];
          g.order.clear();
          for (auto &a : e->ordered)
            if (std::find(selected.begin(), selected.end(), a.id) !=
                selected.end())
              g.order.push_back(a.id);
          e->show();
          SendMessageW(e->apps, LB_SETCARETINDEX, target, FALSE);
        }
      } else if (id == IDOK) {
        try {
          e->capture();
          save(e->groups);
          e->done = true;
        } catch (const std::exception &error) {
          MessageBoxW(window, wide(error.what()).c_str(), L"Kalwer",
                      MB_OK | MB_ICONERROR);
        }
      } else if (id == IDCANCEL)
        e->done = true;
      return 0;
    }
    return DefWindowProcW(window, message, w, l);
  };
  if (!GetClassInfoW(wc.hInstance, cls, &wc))
    RegisterClassW(&wc);
  Editor editor;
  editor.groups = load();
  editor.catalog = std::move(catalog);
  auto dpi = owner ? GetDpiForWindow(owner) : 96;
  if (!dpi)
    dpi = 96;
  auto px = [&](int n) { return MulDiv(n, dpi, 96); };
  auto font =
      CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  editor.window =
      CreateWindowExW(WS_EX_DLGMODALFRAME, cls, L"App groups",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, px(100), px(100),
                      px(660), px(510), owner, nullptr, wc.hInstance, &editor);
  auto child = [&](const wchar_t *type, const wchar_t *label, DWORD style,
                   int id, int x, int y, int width, int height) {
    auto control = CreateWindowW(
        type, label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, px(x), px(y),
        px(width), px(height), editor.window,
        reinterpret_cast<HMENU>(INT_PTR(id)), wc.hInstance, nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    return control;
  };
  editor.list = child(L"LISTBOX", L"", WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 100,
                      12, 14, 170, 360);
  editor.apps = child(L"LISTBOX", L"",
                      WS_BORDER | LBS_EXTENDEDSEL | LBS_NOTIFY | WS_VSCROLL,
                      101, 195, 14, 438, 360);
  editor.automatic =
      child(L"BUTTON", L"Include newly discovered apps automatically",
            BS_AUTOCHECKBOX, 102, 195, 380, 430, 28);
  child(L"BUTTON", L"Add", 0, 104, 12, 382, 78, 30);
  child(L"BUTTON", L"Remove", 0, 105, 96, 382, 86, 30);
  child(L"BUTTON", L"Move up", 0, 106, 195, 418, 90, 30);
  child(L"BUTTON", L"Move down", 0, 107, 291, 418, 100, 30);
  child(L"BUTTON", L"Save", BS_DEFPUSHBUTTON, IDOK, 440, 418, 90, 30);
  child(L"BUTTON", L"Cancel", 0, IDCANCEL, 536, 418, 90, 30);
  editor.populate();
  EnableWindow(owner, FALSE);
  ShowWindow(editor.window, SW_SHOW);
  MSG msg{};
  while (!editor.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(editor.window, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  DestroyWindow(editor.window);
  DeleteObject(font);
  EnableWindow(owner, TRUE);
  SetForegroundWindow(owner);
}
} // namespace kalwer::wingroups
