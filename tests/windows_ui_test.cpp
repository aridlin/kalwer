// Run on an isolated Windows CI account (no existing Kalwer configuration).
#define wWinMain kalwer_application_entry
#include "../windows/kalwer_windows.cpp"
#undef wWinMain
#include <cassert>
#include <iostream>
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    assert(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)));
    state.instance=GetModuleHandleW(nullptr);
    const auto directory=local_data_directory();
    assert(!std::filesystem::exists(directory));
    kalwer::appearance.directory=directory;
    for(float scale:{1.f,1.5f,2.f}) {
        state.render.scale=scale;
        open_settings_popup();
        assert(state.settings_window && IsWindowVisible(state.settings_window));
        RECT client{};GetClientRect(state.settings_window,&client);
        for(int row=0;row<13;++row) {
            HWND control=GetDlgItem(state.settings_window,200+row);
            assert(control);
            RECT bounds{};GetWindowRect(control,&bounds);
            MapWindowPoints(nullptr,state.settings_window,reinterpret_cast<POINT*>(&bounds),2);
            assert(bounds.top>=0 && bounds.bottom<=client.bottom && bounds.left>=0 && bounds.right<=client.right);
        }
        HWND same=state.settings_window;open_settings_popup();assert(state.settings_window==same);
        auto before=kalwer::appearance.bw;
        SendMessageW(GetDlgItem(same,210),BM_CLICK,0,0);
        assert(kalwer::appearance.bw!=before);
        auto delay=state.prompt_retention_ms;
        SendMessageW(GetDlgItem(same,401),BM_CLICK,0,0);
        assert(state.prompt_retention_ms==delay+250);
        SendMessageW(GetDlgItem(same,301),BM_CLICK,0,0);
        assert(state.prompt_retention_ms==delay);
        SendMessageW(GetDlgItem(same,IDOK),BM_CLICK,0,0);
        assert(!state.settings_window);
    }
    auto apps=std::make_shared<std::vector<AppEntry>>();
    for(int i=0;i<20000;++i) {AppEntry app; app.title=L"Application "+std::to_wstring(i);app.folded=lower_copy(app.title);app.link=app.title;apps->push_back(std::move(app));}
    app_search.submit({L"zzzz",apps,{}});
    auto latest=app_search.submit({L"application 19999",apps,{L"application 19999"}});
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    bool done=false;
    while(std::chrono::steady_clock::now()<deadline) {
        if(auto reply=app_search.poll()) {assert(reply->generation==latest && !reply->value.empty() && reply->value.front().pinned);done=true;break;}
        Sleep(1);
    }
    assert(done);
    app_search.stop();icon_worker.stop();
    std::filesystem::remove_all(directory);
    CoUninitialize();
    std::cout<<"Settings popup layout at 100/150/200 percent, singleton, checkbox and numeric input, close, and 20k-app latest-query search passed.\n";
}
