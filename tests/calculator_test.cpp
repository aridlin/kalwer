// Exercise the real platform parser and result rows without opening a window.
#ifdef _WIN32
#define wWinMain kalwer_application_entry
#include "../windows/kalwer_windows.cpp"
#undef wWinMain
#define CALC_TEXT(s) L##s
#else
#define main kalwer_application_entry
#include "../main.cpp"
#undef main
#define CALC_TEXT(s) s
#endif
#include <cassert>
#include <clocale>
#include <iostream>

int main() {
    for (const char* locale : {"C", "pl_PL.UTF-8"}) {
        // The Polish locale is optional on CI; always exercise the C locale.
        if (!std::setlocale(LC_NUMERIC, locale)) continue;
        for (const auto& input : {CALC_TEXT("1.5"), CALC_TEXT("1,5"), CALC_TEXT(" 1,5 ")}) {
            const auto rows = calculator_results(input);
            assert(rows.size() >= 2);
#ifdef _WIN32
            assert(rows[0].payload == L"1.5");
            assert(rows[1].payload == L"3/2");
#else
            assert(rows[0].identifier == "1.5");
            assert(rows[1].identifier == "3/2");
#endif
        }
        for (const auto& input : {CALC_TEXT("0,5 + .25"), CALC_TEXT("-1,5"), CALC_TEXT(",5"),
                                 CALC_TEXT("sqrt(2,25)"), CALC_TEXT("1,5e1 + 0.5"),
                                 CALC_TEXT("1/2"), CALC_TEXT("2+3*4")}) {
            assert(!calculator_results(input).empty());
        }
        for (const auto& input : {CALC_TEXT("1,2.3"), CALC_TEXT("1,,5"), CALC_TEXT("1..5"),
                                 CALC_TEXT("1 2,5"), CALC_TEXT(","), CALC_TEXT("."),
                                 CALC_TEXT("firefox"), CALC_TEXT("1/0"), CALC_TEXT("sqrt(-1)")}) {
            assert(calculator_results(input).empty());
        }
    }
#ifdef _WIN32
    state.instance = GetModuleHandleW(nullptr);
    state.edit = CreateWindowW(L"EDIT", L"", 0, 0, 0, 100, 20,
                              nullptr, nullptr, state.instance, nullptr);
    assert(state.edit);
    AppEntry app;
    app.title = L"Favorite shortcut test";
    app.link = L"favorite-shortcut-test.exe";
    state.app_snapshot = std::make_shared<const std::vector<AppEntry>>(std::vector<AppEntry>{app});
    state.results = {app};
    state.selection = 0;
    edit_window_proc(state.edit, WM_SYSKEYDOWN, VK_RETURN, 1LL << 29);
    assert(state.favorites.size() == 1);
    // Complete the async refresh before toggling it off.
    state.search_pending = false;
    state.results = {app};
    edit_window_proc(state.edit, WM_SYSKEYDOWN, VK_RETURN, 1LL << 29);
    assert(state.favorites.empty());
    DestroyWindow(state.edit);
    state.edit = nullptr;
#else
    const auto config = std::filesystem::temp_directory_path() /
        ("kalwer-shortcut-test-" + std::to_string(getpid()));
    g_setenv("XDG_CONFIG_HOME", config.c_str(), TRUE);
    Result app;
    app.identifier = "favorite-shortcut-test";
    app.provider = "applications";
    state.results = {app};
    state.selection = 0;
    GdkEventKey key{};
    key.keyval = GDK_KEY_Return;
    key.state = GDK_MOD1_MASK;
    assert(on_entry_key(nullptr, &key, nullptr));
    assert(state.favorite_keys.size() == 1);
    assert(state.results[0].pinned);
    assert(on_entry_key(nullptr, &key, nullptr));
    assert(state.favorite_keys.empty());
    assert(!state.results[0].pinned);
    std::filesystem::remove_all(config);
#endif
    std::cout << "PASS: dot/comma decimals, 3/2 rows, invalid input and Alt+Enter favorites\n";
}
