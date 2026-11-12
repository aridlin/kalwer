#pragma once
#include <atomic>
#include <evr.h>
#include <memory>
#include <mfapi.h>
#include <mfidl.h>
#include <mutex>
#include <thread>
#include <wrl/client.h>
#ifdef _MSC_VER
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "evr.lib")
#endif
namespace kalwer::winmedia {
using Microsoft::WRL::ComPtr;
struct Module {
  HMODULE handle = nullptr;
  explicit Module(const wchar_t *name) {
    handle = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  }
  ~Module() {
    if (handle)
      FreeLibrary(handle);
  }
};
struct Native {
  Module platform{L"mfplat.dll"}, media{L"mf.dll"};
  template <class Fn> Fn get(const char *name) {
    auto address =
        platform.handle ? GetProcAddress(platform.handle, name) : nullptr;
    if (!address && media.handle)
      address = GetProcAddress(media.handle, name);
    if (!address)
      throw std::runtime_error("Native media is unavailable. Use Open.");
    return reinterpret_cast<Fn>(address);
  }
  decltype(&MFStartup) startup = get<decltype(&MFStartup)>("MFStartup");
  decltype(&MFShutdown) shutdown = get<decltype(&MFShutdown)>("MFShutdown");
  decltype(&MFCreateMediaSession) session =
      get<decltype(&MFCreateMediaSession)>("MFCreateMediaSession");
  decltype(&MFCreateSourceResolver) resolver =
      get<decltype(&MFCreateSourceResolver)>("MFCreateSourceResolver");
  decltype(&MFCreateTopology) topology =
      get<decltype(&MFCreateTopology)>("MFCreateTopology");
  decltype(&MFCreateTopologyNode) node =
      get<decltype(&MFCreateTopologyNode)>("MFCreateTopologyNode");
  decltype(&MFCreateAudioRendererActivate) audio =
      get<decltype(&MFCreateAudioRendererActivate)>(
          "MFCreateAudioRendererActivate");
  decltype(&MFCreateVideoRendererActivate) video =
      get<decltype(&MFCreateVideoRendererActivate)>(
          "MFCreateVideoRendererActivate");
};
struct State {
  std::mutex mutex;
  ComPtr<IMFMediaSession> session;
  std::atomic<bool> closed = false, ready = false, wanted = false,
                    playing = false, failed = false, ended = false;
  HWND window = nullptr;
  void play() {
    wanted = true;
    std::lock_guard lock(mutex);
    if (session && ready && !closed) {
      PROPVARIANT position{};
      position.vt = ended ? VT_I8 : VT_EMPTY;
      position.hVal.QuadPart = 0;
      ended = false;
      if (SUCCEEDED(session->Start(&GUID_NULL, &position)))
        playing = true;
    }
  }
  void pause() {
    wanted = false;
    std::lock_guard lock(mutex);
    if (session && ready && !closed)
      session->Pause();
    playing = false;
  }
  void close() {
    closed = true;
    wanted = false;
    std::lock_guard lock(mutex);
    if (session)
      session->Shutdown();
    playing = false;
  }
};
inline void require(HRESULT result) {
  if (FAILED(result))
    throw std::runtime_error("Native media preview unavailable. Use Open.");
}
struct Player {
  std::shared_ptr<State> state = std::make_shared<State>();
  Player(HWND parent, const std::filesystem::path &path) {
    state->window = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | WS_CLIPSIBLINGS | SS_BLACKRECT, 40, 210,
        440, 250, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    auto s = state;
    std::thread([s, path] {
      CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      bool started = false;
      std::unique_ptr<Native> native;
      ComPtr<IMFMediaSource> source;
      try {
        native = std::make_unique<Native>();
        require(native->startup(MF_VERSION, MFSTARTUP_FULL));
        started = true;
        ComPtr<IMFMediaSession> session;
        require(native->session(nullptr, session.GetAddressOf()));
        {
          std::lock_guard lock(s->mutex);
          if (s->closed) {
            session->Shutdown();
            throw std::runtime_error("Closed");
          }
          s->session = session;
        }
        ComPtr<IMFSourceResolver> resolver;
        require(native->resolver(resolver.GetAddressOf()));
        MF_OBJECT_TYPE type = MF_OBJECT_INVALID;
        ComPtr<IUnknown> object;
        require(resolver->CreateObjectFromURL(
            path.c_str(), MF_RESOLUTION_MEDIASOURCE, nullptr, &type,
            object.GetAddressOf()));
        require(object.As(&source));
        ComPtr<IMFPresentationDescriptor> presentation;
        require(
            source->CreatePresentationDescriptor(presentation.GetAddressOf()));
        ComPtr<IMFTopology> topology;
        require(native->topology(topology.GetAddressOf()));
        DWORD count = 0;
        require(presentation->GetStreamDescriptorCount(&count));
        for (DWORD i = 0; i < count; i++) {
          BOOL selected = FALSE;
          ComPtr<IMFStreamDescriptor> stream;
          require(presentation->GetStreamDescriptorByIndex(
              i, &selected, stream.GetAddressOf()));
          if (!selected)
            continue;
          ComPtr<IMFMediaTypeHandler> handler;
          GUID major{};
          require(stream->GetMediaTypeHandler(handler.GetAddressOf()));
          require(handler->GetMajorType(&major));
          ComPtr<IMFActivate> activate;
          if (major == MFMediaType_Audio)
            require(native->audio(activate.GetAddressOf()));
          else if (major == MFMediaType_Video)
            require(native->video(s->window, activate.GetAddressOf()));
          else {
            presentation->DeselectStream(i);
            continue;
          }
          ComPtr<IMFTopologyNode> input, output;
          require(native->node(MF_TOPOLOGY_SOURCESTREAM_NODE,
                               input.GetAddressOf()));
          require(input->SetUnknown(MF_TOPONODE_SOURCE, source.Get()));
          require(input->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR,
                                    presentation.Get()));
          require(
              input->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, stream.Get()));
          require(native->node(MF_TOPOLOGY_OUTPUT_NODE, output.GetAddressOf()));
          require(output->SetObject(activate.Get()));
          require(output->SetUINT32(MF_TOPONODE_STREAMID, 0));
          require(output->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE));
          require(topology->AddNode(input.Get()));
          require(topology->AddNode(output.Get()));
          require(input->ConnectOutput(0, output.Get(), 0));
        }
        require(session->SetTopology(0, topology.Get()));
        while (!s->closed) {
          ComPtr<IMFMediaEvent> event;
          HRESULT result = session->GetEvent(0, event.GetAddressOf());
          if (FAILED(result))
            break;
          MediaEventType kind = MEUnknown;
          event->GetType(&kind);
          HRESULT status = S_OK;
          event->GetStatus(&status);
          if (FAILED(status))
            throw std::runtime_error("Media error");
          if (kind == MESessionTopologyStatus) {
            UINT32 status = 0;
            event->GetUINT32(MF_EVENT_TOPOLOGY_STATUS, &status);
            if (status == MF_TOPOSTATUS_READY) {
              s->ready = true;
              if (s->wanted)
                s->play();
            }
          }
          if (kind == MEEndOfPresentation || kind == MESessionEnded) {
            s->ended = true;
            s->playing = false;
            s->wanted = false;
          }
        }
      } catch (...) {
        if (!s->closed)
          s->failed = true;
      }
      if (source)
        source->Shutdown();
      {
        std::lock_guard lock(s->mutex);
        if (s->session) {
          s->session->Shutdown();
          s->session.Reset();
        }
      }
      source.Reset();
      if (started)
        native->shutdown();
      CoUninitialize();
    }).detach();
  }
  ~Player() {
    state->close();
    if (state->window)
      DestroyWindow(state->window);
  }
  void toggle() {
    if (state->failed)
      return;
    if (state->playing || state->wanted)
      state->pause();
    else {
      ShowWindow(state->window, SW_SHOW);
      state->play();
    }
  }
  void place(int x, int y, int width, int height) {
    if (state->window)
      SetWindowPos(state->window, HWND_TOP, x, y, width, height,
                   SWP_NOACTIVATE);
  }
  void pause() { state->pause(); }
};
} // namespace kalwer::winmedia
