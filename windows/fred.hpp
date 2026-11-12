#pragma once
#include "../fred/api.h"
#include "services.hpp"
#include <array>
namespace kalwer::winfred {
struct Session {
  HMODULE library = nullptr;
  const KalwerFredApi *api = nullptr;
  void *game = nullptr;
  std::uint32_t held = 0, pressed = 0;
  bool focused = false;
  ULONGLONG last = 0;
  std::string error;
  Session() {
    auto path = winservices::data() / L"fred" / L"kalwer-fred.dll";
    library = LoadLibraryExW(path.c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                 LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (library) {
      auto entry = reinterpret_cast<const KalwerFredApi *(*)()>(
          GetProcAddress(library, "kalwer_fred_api"));
      if (entry)
        api = entry();
      if (api && api->version == 1 && api->create && api->destroy &&
          api->step && api->pixels)
        game = api->create();
    }
    if (!game)
      error = "Fred is not installed. Build the optional Windows module with "
              "your own original reference; see fred/README.md.";
  }
  ~Session() {
    if (game)
      api->destroy(game);
    if (library)
      FreeLibrary(library);
  }
  void focus(bool active) {
    focused = active;
    held = pressed = 0;
    last = GetTickCount64();
  }
  void key(UINT key, bool down) {
    unsigned bit = key == VK_LEFT || key == 'Q'    ? FredLeft
                   : key == VK_RIGHT || key == 'W' ? FredRight
                   : key == VK_UP || key == 'R'    ? FredUp
                   : key == VK_DOWN || key == 'E'  ? FredDown
                   : key == VK_SPACE || key == 'T' ? FredFire
                   : key == VK_RETURN              ? FredStart
                                                   : 0;
    if (down) {
      if (!(held & bit))
        pressed |= bit;
      held |= bit;
    } else
      held &= ~bit;
  }
  bool tick() {
    if (!focused || !game)
      return false;
    auto now = GetTickCount64();
    if (now - last < 180)
      return false;
    last = now;
    auto buttons = held | pressed;
    pressed = 0;
    if (!api->step(game, buttons)) {
      error = "Fred stopped. Reopen /fred to try again.";
      api->destroy(game);
      game = nullptr;
    }
    return true;
  }
};
} // namespace kalwer::winfred
