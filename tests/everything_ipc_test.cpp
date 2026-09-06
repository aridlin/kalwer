#include "../system_file_index.hpp"
#include <cassert>
#include <future>
#include <iostream>
using namespace kalwer_files;
std::vector<unsigned char> packet() {
    constexpr size_t base=offsetof(EVERYTHING_IPC_LISTW,items);
    std::vector<unsigned char> bytes(base+2*sizeof(EVERYTHING_IPC_ITEMW));
    auto add=[&](const std::wstring& s){DWORD offset=bytes.size();const auto* p=reinterpret_cast<const unsigned char*>(s.c_str());bytes.insert(bytes.end(),p,p+(s.size()+1)*sizeof(wchar_t));return offset;};
    DWORD n1=add(L"报告.txt"),p1=add(L"C:\\Documents"),n2=add(L"report.txt"),p2=add(L"D:\\Projects");
    auto* list=reinterpret_cast<EVERYTHING_IPC_LISTW*>(bytes.data());list->numitems=2;list->totitems=2;
    list->items[0]={0,n1,p1};list->items[1]={0,n2,p2};return bytes;
}
LRESULT CALLBACK engine_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==EVERYTHING_WM_IPC&&wp==EVERYTHING_IPC_IS_DB_LOADED)return 1;
    if(msg==WM_COPYDATA) {
        const auto* data=reinterpret_cast<const COPYDATASTRUCT*>(lp);
        assert(data->dwData==EVERYTHING_IPC_COPYDATAQUERYW);
        const auto* q=static_cast<const EVERYTHING_IPC_QUERYW*>(data->lpData);
        if(std::wstring(q->search_string)==L"slow")return 1;
        auto bytes=packet();COPYDATASTRUCT reply{q->reply_copydata_message,static_cast<DWORD>(bytes.size()),bytes.data()};
        SendMessageW(reinterpret_cast<HWND>(static_cast<ULONG_PTR>(q->reply_hwnd)),WM_COPYDATA,reinterpret_cast<WPARAM>(w),reinterpret_cast<LPARAM>(&reply));return 1;
    }
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,msg,wp,lp);
}
Reply await_reply(Index& index,unsigned long long id){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<deadline){auto r=index.poll();if(r.request==id&&r.revision)return r;Sleep(5);}throw std::runtime_error("No Everything reply");}
int main() {
    auto bytes=packet();auto entries=decode_everything(bytes);assert(entries.size()==2&&entries[0].path==text(fs::path(L"C:\\Documents\\报告.txt")));
    reinterpret_cast<EVERYTHING_IPC_LISTW*>(bytes.data())->numitems=0xffffffff;assert(decode_everything(bytes).empty());
    bytes=packet();reinterpret_cast<EVERYTHING_IPC_LISTW*>(bytes.data())->items[0].filename_offset=0xfffffffe;assert(decode_everything(bytes).size()==1);
    std::promise<HWND> ready;auto future=ready.get_future();
    std::thread engine([&]{WNDCLASSW wc{};wc.lpfnWndProc=engine_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=EVERYTHING_IPC_WNDCLASSW;RegisterClassW(&wc);auto window=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,nullptr,nullptr,wc.hInstance,nullptr);ready.set_value(window);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0)DispatchMessageW(&msg);});
    HWND window=future.get();assert(window);
    {
        Index index;auto reply=await_reply(index,index.request("report"));assert(reply.entries.size()==2&&reply.entries[0].name=="report.txt");
        index.request("slow");Sleep(50);auto start=std::chrono::steady_clock::now();reply=await_reply(index,index.request("report"));assert(reply.entries.size()==2&&std::chrono::steady_clock::now()-start<std::chrono::milliseconds(500));
    }
    PostMessageW(window,WM_CLOSE,0,0);engine.join();std::cout<<"Passed Everything Unicode IPC, reply validation, ranking and cancelled-query tests.\n";
}
