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
    // Exercise real native editing, including DPI bounds and undo, without a GPU.
    WNDCLASSW input_class{};input_class.hInstance=state.instance;input_class.lpszClassName=L"KalwerInlineTest";
    input_class.lpfnWndProc=+[](HWND window,UINT message,WPARAM w,LPARAM l)->LRESULT{
        if(message==WM_COMMAND && HIWORD(w)==EN_CHANGE){layout_inline_input();return 0;}
        return DefWindowProcW(window,message,w,l);
    };
    RegisterClassW(&input_class);
    state.window=CreateWindowW(input_class.lpszClassName,L"Inline test",WS_OVERLAPPEDWINDOW,0,0,1400,1400,nullptr,nullptr,state.instance,nullptr);
    state.edit=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,0,0,548,25,state.window,reinterpret_cast<HMENU>(100),state.instance,nullptr);
    state.edit_proc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state.edit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(edit_window_proc)));
    for(float scale:{1.f,1.5f,2.f}) {
        state.render.scale=scale;state.opening=state.closing=state.popup_open=false;
        SetWindowTextW(state.edit,L"> echo first");SendMessageW(state.edit,EM_SETSEL,12,12);
        open_multiline_editor();assert(!multiline_window);
        assert(window_text(state.edit)==L"> echo first\r\n");
        SendMessageW(state.edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"echo second"));
        assert(window_text(state.edit)==L"> echo first\r\necho second");
        assert(input_extra_height>=25 && GetParent(state.edit)==state.window);
        RECT bounds{};GetWindowRect(state.edit,&bounds);MapWindowPoints(nullptr,state.window,reinterpret_cast<POINT*>(&bounds),2);
        assert(std::abs(bounds.left-67*scale)<=1 && bounds.bottom<=int((search_height()+12)*scale));
        // A selection spanning the line break supports native replacement and undo.
        SendMessageW(state.edit,EM_SETSEL,7,18);SendMessageW(state.edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"replacement"));
        SendMessageW(state.edit,EM_UNDO,0,0);assert(window_text(state.edit)==L"> echo first\r\necho second");
        SendMessageW(state.edit,EM_SETSEL,0,-1);SendMessageW(state.edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L""));
        assert(input_extra_height==0);
    }
    DestroyWindow(state.window);state.window=state.edit=nullptr;
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
    auto job=create_command_job(L"echo kalwer-first-line\necho kalwer-second-line");
    assert(job);
    assert(WaitForSingleObject(job->process,15000)==WAIT_OBJECT_0);
    if(job->waiter.joinable())job->waiter.join();
    {std::lock_guard lock(job->output_mutex);assert(job->output.find("kalwer-first-line")!=std::string::npos);assert(job->output.find("kalwer-second-line")!=std::string::npos);}
    auto script=job->script.path;
    CloseHandle(job->input_write);CloseHandle(job->output_read);CloseHandle(job->process_thread);CloseHandle(job->process);job.reset();assert(!std::filesystem::exists(script));
    auto blocked=create_command_job(L"ping -n 2 127.0.0.1 >nul");assert(blocked);state.popup_job=blocked.get();
    std::string large_input(1024*1024,'x');auto queued_at=std::chrono::steady_clock::now();write_job_input(large_input.data(),static_cast<DWORD>(large_input.size()));
    assert(std::chrono::steady_clock::now()-queued_at<std::chrono::milliseconds(100));
    auto stopped_by=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(!blocked->reaped.load() && std::chrono::steady_clock::now()<stopped_by)Sleep(10);
    assert(blocked->reaped.load());blocked->waiter.join();state.popup_job=nullptr;
    CloseHandle(blocked->input_write);CloseHandle(blocked->output_read);CloseHandle(blocked->process_thread);CloseHandle(blocked->process);blocked.reset();
    app_search.stop();icon_worker.stop();
    std::filesystem::remove_all(directory);
    CoUninitialize();
    std::cout<<"Settings popup layout at 100/150/200 percent, singleton, checkbox and numeric input, close, and 20k-app latest-query search passed.\n";
}
