#pragma once
#include "media_player.hpp"
#include "services.hpp"
#include <deque>
#include <functional>
#include <gdiplus.h>
#include <wincodec.h>
#include <wrl/client.h>
#ifdef _MSC_VER
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "uuid.lib")
#endif
#include <set>
#include <shellapi.h>
#include <shobjidl.h>
#include <thread>
#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif
namespace kalwer::winfiles {
using namespace winservices;
inline fs::path known(REFKNOWNFOLDERID id) {
  PWSTR p = nullptr;
  fs::path value;
  if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) {
    value = p;
    CoTaskMemFree(p);
  }
  return value;
}
inline std::wstring lower(std::wstring value) {
  if (value.empty())
    return value;
  int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(),
                        int(value.size()), nullptr, 0, nullptr, nullptr, 0);
  if (!n)
    return value;
  std::wstring out(n, L' ');
  if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(),
                     int(value.size()), out.data(), n, nullptr, nullptr, 0))
    return value;
  return out;
}
inline bool downloads(const fs::path &p) {
  return lower(p.lexically_normal().wstring()) ==
         lower(known(FOLDERID_Downloads).lexically_normal().wstring());
}
struct Bitmap {
  HBITMAP handle = nullptr;
  ~Bitmap() {
    if (handle)
      DeleteObject(handle);
  }
};
struct Gif {
  struct Data {
    std::atomic<bool> closed = false, paused = true, limited = false;
    std::mutex mutex;
    std::shared_ptr<Bitmap> frame;
    unsigned revision = 0;
  };
  std::shared_ptr<Data> data = std::make_shared<Data>();
  unsigned shown = 0;
  explicit Gif(const fs::path &path) {
    auto d = data;
    std::thread([d, path] {
      Gdiplus::GdiplusStartupInput options;
      ULONG_PTR token = 0;
      if (Gdiplus::GdiplusStartup(&token, &options, nullptr) != Gdiplus::Ok)
        return;
      {
        Gdiplus::Bitmap image(path.c_str());
        auto width = image.GetWidth(), height = image.GetHeight();
        if (image.GetLastStatus() == Gdiplus::Ok && width && height &&
            std::uint64_t(width) * height <= 16000000) {
          auto count = image.GetFrameCount(&Gdiplus::FrameDimensionTime);
          if (!count)
            count = 1;
          if (count > 600 || std::uint64_t(width) * height * count > 16000000) {
            d->limited = true;
            count = 1;
          }
          double scale = std::min(1., 640. / std::max(width, height));
          UINT w = std::max(1U, UINT(width * scale)),
               h = std::max(1U, UINT(height * scale));
          std::vector<UINT> delays(count, 100);
          auto bytes = image.GetPropertyItemSize(PropertyTagFrameDelay);
          if (bytes && bytes < 65536) {
            std::vector<unsigned char> storage(bytes);
            auto item =
                reinterpret_cast<Gdiplus::PropertyItem *>(storage.data());
            if (image.GetPropertyItem(PropertyTagFrameDelay, bytes, item) ==
                    Gdiplus::Ok &&
                item->length >= count * sizeof(UINT))
              for (UINT i = 0; i < count; i++)
                delays[i] = std::clamp(
                    static_cast<UINT *>(item->value)[i] * 10U, 40U, 10000U);
          }
          for (UINT index = 0; !d->closed; index = (index + 1) % count) {
            if (index && d->paused) {
              while (d->paused && !d->closed)
                Sleep(40);
              if (d->closed)
                break;
            }
            image.SelectActiveFrame(&Gdiplus::FrameDimensionTime, index);
            Gdiplus::Bitmap frame_bitmap(w, h, PixelFormat32bppPARGB);
            Gdiplus::Graphics paint(&frame_bitmap);
            paint.Clear(Gdiplus::Color(0, 0, 0, 0));
            paint.DrawImage(&image, 0, 0, w, h);
            auto bitmap = std::make_shared<Bitmap>();
            if (frame_bitmap.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0),
                                        &bitmap->handle) == Gdiplus::Ok) {
              std::lock_guard lock(d->mutex);
              d->frame = bitmap;
              d->revision++;
            }
            if (count == 1)
              break;
            for (UINT elapsed = 0; elapsed < delays[index] && !d->closed;
                 elapsed += 20)
              Sleep(20);
          }
        }
      }
      Gdiplus::GdiplusShutdown(token);
    }).detach();
  }
  ~Gif() { data->closed = true; }
};
inline std::shared_ptr<Bitmap> image_thumbnail(const fs::path &path,
                                               int maximum) {
  Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  Microsoft::WRL::ComPtr<IWICBitmapScaler> scaled;
  Microsoft::WRL::ComPtr<IWICFormatConverter> converted;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                              CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(factory.GetAddressOf()))) ||
      FAILED(factory->CreateDecoderFromFilename(
          path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
          decoder.GetAddressOf())) ||
      FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
    return {};
  Microsoft::WRL::ComPtr<IWICBitmapSource> oriented;
  frame.As(&oriented);
  Microsoft::WRL::ComPtr<IWICMetadataQueryReader> metadata;
  PROPVARIANT orientation{};
  unsigned rotate = 1;
  if (SUCCEEDED(frame->GetMetadataQueryReader(metadata.GetAddressOf()))) {
    auto result =
        metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}", &orientation);
    if (FAILED(result))
      result = metadata->GetMetadataByName(L"/ifd/{ushort=274}", &orientation);
    if (SUCCEEDED(result)) {
      if (orientation.vt == VT_UI2)
        rotate = orientation.uiVal;
      else if (orientation.vt == VT_UI4)
        rotate = orientation.ulVal;
    }
    PropVariantClear(&orientation);
  }
  auto transform =
      rotate == 2   ? WICBitmapTransformFlipHorizontal
      : rotate == 3 ? WICBitmapTransformRotate180
      : rotate == 4 ? WICBitmapTransformFlipVertical
      : rotate == 5
          ? WICBitmapTransformOptions(WICBitmapTransformRotate90 |
                                      WICBitmapTransformFlipHorizontal)
      : rotate == 6 ? WICBitmapTransformRotate90
      : rotate == 7
          ? WICBitmapTransformOptions(WICBitmapTransformRotate270 |
                                      WICBitmapTransformFlipHorizontal)
      : rotate == 8 ? WICBitmapTransformRotate270
                    : WICBitmapTransformRotate0;
  Microsoft::WRL::ComPtr<IWICBitmapFlipRotator> rotated;
  if (transform != WICBitmapTransformRotate0 &&
      SUCCEEDED(factory->CreateBitmapFlipRotator(rotated.GetAddressOf())) &&
      SUCCEEDED(rotated->Initialize(frame.Get(), transform)))
    rotated.As(&oriented);
  UINT width = 0, height = 0;
  if (FAILED(oriented->GetSize(&width, &height)) || !width || !height ||
      std::uint64_t(width) * height > 16000000)
    return {};
  double factor = std::min(1., double(maximum) / std::max(width, height));
  UINT w = std::max(1U, UINT(width * factor)),
       h = std::max(1U, UINT(height * factor));
  if (FAILED(factory->CreateBitmapScaler(scaled.GetAddressOf())) ||
      FAILED(scaled->Initialize(oriented.Get(), w, h,
                                WICBitmapInterpolationModeFant)) ||
      FAILED(factory->CreateFormatConverter(converted.GetAddressOf())) ||
      FAILED(converted->Initialize(scaled.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom)))
    return {};
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = LONG(w);
  info.bmiHeader.biHeight = -LONG(h);
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void *bytes = nullptr;
  auto result = std::make_shared<Bitmap>();
  result->handle =
      CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bytes, nullptr, 0);
  if (!result->handle ||
      FAILED(converted->CopyPixels(nullptr, w * 4, w * h * 4,
                                   static_cast<BYTE *>(bytes))))
    return {};
  return result;
}
inline std::shared_ptr<Bitmap> thumbnail(const fs::path &path, int size = 160) {
  if (auto image = image_thumbnail(path, size))
    return image;
  IShellItemImageFactory *factory = nullptr;
  if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr,
                                         IID_PPV_ARGS(&factory))))
    return image_thumbnail(path, size);
  auto out = std::make_shared<Bitmap>();
  auto ok =
      factory->GetImage(SIZE{size, size}, SIIGBF_RESIZETOFIT, &out->handle);
  factory->Release();
  BITMAP info{};
  if (SUCCEEDED(ok) && GetObjectW(out->handle, sizeof(info), &info) &&
      info.bmWidth > 1 && info.bmHeight > 1)
    return out;
  return {};
}
struct DropSource : IDropSource {
  ULONG refs = 1;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
    if (iid == IID_IUnknown || iid == IID_IDropSource) {
      *out = static_cast<IDropSource *>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    auto n = --refs;
    if (!n)
      delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,
                                              DWORD keys) override {
    return escape                 ? DRAGDROP_S_CANCEL
           : !(keys & MK_LBUTTON) ? DRAGDROP_S_DROP
                                  : S_OK;
  }
  HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
    return DRAGDROP_S_USEDEFAULTCURSORS;
  }
};
struct FormatEnum : IEnumFORMATETC {
  LONG refs = 1;
  ULONG index = 0;
  std::vector<FORMATETC> formats;
  explicit FormatEnum(std::vector<FORMATETC> values)
      : formats(std::move(values)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (id == IID_IUnknown || id == IID_IEnumFORMATETC) {
      *out = static_cast<IEnumFORMATETC *>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&refs);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    auto count = InterlockedDecrement(&refs);
    if (!count)
      delete this;
    return count;
  }
  HRESULT STDMETHODCALLTYPE Next(ULONG count, FORMATETC *out,
                                 ULONG *fetched) override {
    if (!out || (!fetched && count != 1))
      return E_POINTER;
    ULONG n = 0;
    while (n < count && index < formats.size())
      out[n++] = formats[index++];
    if (fetched)
      *fetched = n;
    return n == count ? S_OK : S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
    auto before = index;
    index = std::min<ULONG>(index + count, ULONG(formats.size()));
    return index - before == count ? S_OK : S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE Reset() override {
    index = 0;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC **out) override {
    if (!out)
      return E_POINTER;
    auto copy = new FormatEnum(formats);
    copy->index = index;
    *out = copy;
    return S_OK;
  }
};
struct FileObject : IDataObject {
  LONG refs = 1;
  std::wstring path;
  CLIPFORMAT effect =
      CLIPFORMAT(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT));
  explicit FileObject(std::wstring value) : path(std::move(value)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (id == IID_IUnknown || id == IID_IDataObject) {
      *out = static_cast<IDataObject *>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&refs);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    auto count = InterlockedDecrement(&refs);
    if (!count)
      delete this;
    return count;
  }
  HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *format) override {
    if (!format)
      return E_POINTER;
    if (!(format->tymed & TYMED_HGLOBAL) ||
        format->dwAspect != DVASPECT_CONTENT || format->lindex != -1)
      return DV_E_FORMATETC;
    return format->cfFormat == CF_HDROP || format->cfFormat == CF_UNICODETEXT ||
                   format->cfFormat == effect
               ? S_OK
               : DV_E_FORMATETC;
  }
  HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format,
                                    STGMEDIUM *out) override {
    if (!out)
      return E_POINTER;
    ZeroMemory(out, sizeof(*out));
    if (FAILED(QueryGetData(format)))
      return DV_E_FORMATETC;
    size_t bytes = format->cfFormat == CF_HDROP
                       ? sizeof(DROPFILES) + (path.size() + 2) * sizeof(wchar_t)
                   : format->cfFormat == effect
                       ? sizeof(DWORD)
                       : (path.size() + 1) * sizeof(wchar_t);
    auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!memory)
      return E_OUTOFMEMORY;
    auto raw = GlobalLock(memory);
    if (!raw) {
      GlobalFree(memory);
      return E_OUTOFMEMORY;
    }
    if (format->cfFormat == CF_HDROP) {
      auto drop = static_cast<DROPFILES *>(raw);
      drop->pFiles = sizeof(DROPFILES);
      drop->fWide = TRUE;
      memcpy(static_cast<char *>(raw) + sizeof(DROPFILES), path.c_str(),
             (path.size() + 1) * sizeof(wchar_t));
    } else if (format->cfFormat == effect)
      *static_cast<DWORD *>(raw) = DROPEFFECT_COPY;
    else
      memcpy(raw, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(memory);
    out->tymed = TYMED_HGLOBAL;
    out->hGlobal = memory;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override {
    return DATA_E_FORMATETC;
  }
  HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *,
                                                  FORMATETC *out) override {
    if (!out)
      return E_POINTER;
    out->ptd = nullptr;
    return DATA_S_SAMEFORMATETC;
  }
  HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,
                                          IEnumFORMATETC **out) override {
    if (!out)
      return E_POINTER;
    if (direction != DATADIR_GET)
      return E_NOTIMPL;
    *out = new FormatEnum(
        {FORMATETC{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
         FORMATETC{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1,
                   TYMED_HGLOBAL},
         FORMATETC{effect, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}});
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *,
                                    DWORD *) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }
  HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }
  HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }
};
inline IDataObject *object(const fs::path &path) {
  std::error_code error;
  if (!fs::exists(path, error))
    return nullptr;
  return new FileObject(fs::absolute(path).wstring());
}
inline void copy(const fs::path &path) {
  auto data = object(path);
  if (!data)
    throw std::runtime_error("Cannot copy this file.");
  auto ok = OleSetClipboard(data);
  if (SUCCEEDED(ok))
    OleFlushClipboard();
  data->Release();
  if (FAILED(ok))
    throw std::runtime_error("Clipboard is unavailable.");
}
inline void drag(const fs::path &path) {
  auto data = object(path);
  if (!data)
    return;
  auto source = new DropSource;
  DWORD effect = 0;
  DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
  source->Release();
  data->Release();
}
struct Prompt {
  bool done = false, ok = false;
  std::wstring value;
  HWND edit = nullptr;
};
inline std::optional<std::wstring> prompt(HWND owner, const std::wstring &title,
                                          const std::wstring &initial = {},
                                          bool password = false,
                                          bool multiline = false) {
  static const wchar_t name[] = L"KalwerValuePrompt";
  WNDCLASSW wc{};
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = name;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  static HBRUSH background = CreateSolidBrush(RGB(8, 26, 24));
  wc.hbrBackground = background;
  wc.lpfnWndProc = +[](HWND w, UINT m, WPARAM a, LPARAM b) -> LRESULT {
    auto p = reinterpret_cast<Prompt *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (m == WM_CREATE) {
      p = static_cast<Prompt *>(
          reinterpret_cast<CREATESTRUCTW *>(b)->lpCreateParams);
      SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
    }
    if (p && m == WM_COMMAND && (LOWORD(a) == IDOK || LOWORD(a) == IDCANCEL)) {
      p->ok = LOWORD(a) == IDOK;
      if (p->ok) {
        int n = GetWindowTextLengthW(p->edit);
        p->value.resize(n + 1);
        GetWindowTextW(p->edit, p->value.data(), n + 1);
        p->value.resize(n);
      }
      p->done = true;
      return 0;
    }
    if (p && m == WM_CLOSE) {
      p->done = true;
      return 0;
    }
    if (m == WM_CTLCOLOREDIT) {
      SetTextColor(reinterpret_cast<HDC>(a), RGB(140, 233, 179));
      SetBkColor(reinterpret_cast<HDC>(a), RGB(8, 26, 24));
      return static_cast<LRESULT>(GetClassLongPtrW(w, GCLP_HBRBACKGROUND));
    }
    return DefWindowProcW(w, m, a, b);
  };
  if (!GetClassInfoW(wc.hInstance, name, &wc))
    RegisterClassW(&wc);
  auto dpi = owner ? GetDpiForWindow(owner) : 96;
  if (!dpi)
    dpi = 96;
  auto px = [&](int n) { return MulDiv(n, dpi, 96); };
  Prompt p;
  auto window = CreateWindowExW(WS_EX_DLGMODALFRAME, name, title.c_str(),
                                WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0,
                                px(480), px(multiline ? 330 : 180), owner,
                                nullptr, GetModuleHandleW(nullptr), &p);
  if (!window)
    return {};
  RECT bounds{};
  GetWindowRect(owner, &bounds);
  SetWindowPos(window, nullptr, bounds.left + px(20), bounds.top + px(80), 0, 0,
               SWP_NOSIZE | SWP_NOZORDER);
  p.edit =
      CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial.c_str(),
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                          (multiline ? ES_MULTILINE | ES_WANTRETURN |
                                           ES_AUTOVSCROLL | WS_VSCROLL
                                     : ES_AUTOHSCROLL) |
                          (password ? ES_PASSWORD : 0),
                      px(14), px(18), px(440), px(multiline ? 180 : 34), window,
                      reinterpret_cast<HMENU>(100), wc.hInstance, nullptr);
  SendMessageW(p.edit, EM_SETLIMITTEXT, password ? 64 : 65536, 0);
  CreateWindowW(L"BUTTON", L"OK",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, px(260),
                px(multiline ? 225 : 75), px(90), px(34), window,
                reinterpret_cast<HMENU>(IDOK), wc.hInstance, nullptr);
  CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                px(360), px(multiline ? 225 : 75), px(90), px(34), window,
                reinterpret_cast<HMENU>(IDCANCEL), wc.hInstance, nullptr);
  auto font =
      CreateFontW(-px(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
  for (int id : {100, IDOK, IDCANCEL})
    SendMessageW(GetDlgItem(window, id), WM_SETFONT,
                 reinterpret_cast<WPARAM>(font), FALSE);
  EnableWindow(owner, FALSE);
  ShowWindow(window, SW_SHOW);
  SetFocus(p.edit);
  MSG message{};
  while (!p.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (message.message == WM_KEYDOWN && message.wParam == 'A' &&
        (GetKeyState(VK_CONTROL) & 0x8000) && GetFocus() == p.edit) {
      SendMessageW(p.edit, EM_SETSEL, 0, -1);
      continue;
    }
    if (message.message == WM_KEYDOWN &&
        (message.wParam == VK_ESCAPE ||
         (message.wParam == VK_RETURN &&
          (!multiline || (GetKeyState(VK_CONTROL) & 0x8000))))) {
      SendMessageW(window, WM_COMMAND,
                   message.wParam == VK_RETURN ? IDOK : IDCANCEL, 0);
      continue;
    }
    if (!IsDialogMessageW(window, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  DestroyWindow(window);
  DeleteObject(font);
  EnableWindow(owner, TRUE);
  SetForegroundWindow(owner);
  return p.ok ? std::optional<std::wstring>(p.value) : std::nullopt;
}
struct Entry {
  fs::path path;
  std::wstring name;
  bool folder = false;
  std::uint64_t date = 0, size = 0;
  std::string url, thumb, kind;
};
inline void sort_entries(std::vector<Entry> &out, bool mixed) {
  std::stable_sort(out.begin(), out.end(), [mixed](auto &a, auto &b) {
    if (!mixed && a.folder != b.folder)
      return a.folder;
    if (a.date != b.date)
      return a.date > b.date;
    return lower(a.name) < lower(b.name);
  });
}
struct Hit {
  float x, y, w, h;
  std::function<void()> action;
};
struct Panel : std::enable_shared_from_this<Panel> {
  HWND owner = nullptr;
  bool incoming = false;
  std::vector<fs::path> shared_paths;
  std::atomic<bool> alive = true;
  bool reload_requested = false;
  bool busy = false, preview = false, choosing = false, review = false,
       meme = false;
  std::string service, query, service_query, cat = "all", message;
  std::vector<std::string> categories;
  std::vector<Entry> entries;
  std::vector<size_t> visible;
  std::vector<fs::path> bookmarks;
  fs::path directory, selected, pending_file;
  int selection = 0, scroll = 0, next = -1, text_scroll = 0, text_lines = 0;
  std::uint64_t generation = 0;
  Http http;
  std::mutex mutex;
  std::deque<std::function<void(Panel &)>> completions;
  std::map<fs::path, std::shared_ptr<Bitmap>> bitmaps;
  std::set<fs::path> image_pending;
  unsigned image_active = 0;
  std::vector<Hit> hits;
  int hovered = -1, pressed = -1;
  bool animating = false;
  std::map<int, float> hover_amount;
  std::unique_ptr<Gif> animation;
  std::unique_ptr<kalwer::winmedia::Player> video;
  bool video_playing = false, video_failed = false, video_wanted = false,
       gif_limited_reported = false;
  std::wstring preview_text;
  std::map<fs::path, std::wstring> note_bodies;
  std::function<void()> changed, close, note_saved;
  ~Panel() { http.cancel(); }
  void shutdown() {
    alive = false;
    http.cancel();
    video.reset();
    animation.reset();
  }
  template <class Work, class Done> void run(Work work, Done done) {
    if (busy || !alive)
      return;
    busy = true;
    message = "Loading…";
    auto self = shared_from_this();
    auto serial = generation;
    std::thread([self, serial, work = std::move(work),
                 done = std::move(done)]() mutable {
      CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      try {
        auto result = work(self->http);
        std::lock_guard lock(self->mutex);
        self->completions.push_back([result = std::move(result),
                                     done = std::move(done),
                                     serial](Panel &p) mutable {
          p.busy = false;
          if (serial != p.generation)
            return;
          p.message.clear();
          done(p, std::move(result));
        });
      } catch (const std::exception &e) {
        auto error = std::string(e.what());
        std::lock_guard lock(self->mutex);
        self->completions.push_back([error, serial](Panel &p) {
          p.busy = false;
          if (serial == p.generation)
            p.message = error;
        });
      }
      CoUninitialize();
    }).detach();
  }
  bool poll() {
    bool changed = false;
    if (animation) {
      if (animation->data->limited && !gif_limited_reported) {
        gif_limited_reported = true;
        message = "Large animation: use Open.";
        changed = true;
      }
      std::lock_guard lock(animation->data->mutex);
      if (animation->shown != animation->data->revision) {
        animation->shown = animation->data->revision;
        bitmaps[selected] = animation->data->frame;
        changed = true;
      }
    }
    if (video) {
      if (video_playing != video->state->playing ||
          video_failed != video->state->failed ||
          video_wanted != video->state->wanted) {
        video_playing = video->state->playing;
        video_failed = video->state->failed;
        video_wanted = video->state->wanted;
        if (video_failed)
          message = "Preview unavailable. Use Open.";
        changed = true;
      }
    }
    std::deque<std::function<void(Panel &)>> jobs;
    {
      std::lock_guard lock(mutex);
      jobs.swap(completions);
    }
    for (auto &job : jobs)
      if (alive)
        job(*this);
    if (!busy && reload_requested && alive) {
      reload_requested = false;
      fetch();
      changed = true;
    }
    return changed || !jobs.empty();
  }
  void filter() {
    visible.clear();
    auto needle = lower(wide(query));
    for (size_t i = 0; i < entries.size(); i++)
      if (lower(entries[i].name + (service == "notes"
                                       ? L" " + note_bodies[entries[i].path]
                                       : L""))
              .find(needle) != std::wstring::npos)
        visible.push_back(i);
    selection = 0;
    scroll = 0;
  }
  void save_places() {
    Json rows = Json::array();
    for (auto &p : bookmarks)
      rows.push_back(utf8(p.wstring()));
    atomic_save(data() / L"file-bookmarks.json", rows.dump());
  }
  void load_places() {
    try {
      for (auto &item : Json::parse(read(data() / L"file-bookmarks.json")))
        bookmarks.push_back(wide(item.get<std::string>()));
    } catch (...) {
      for (auto id :
           {&FOLDERID_Downloads, &FOLDERID_Desktop, &FOLDERID_Documents}) {
        auto path = known(*id);
        if (!path.empty())
          bookmarks.push_back(path);
      }
    }
  }
  void browse(fs::path path) {
    if (busy)
      return;
    if (path.empty())
      path = known(FOLDERID_Downloads);
    auto raw = path.wstring();
    if (raw.starts_with(L"~"))
      path = known(FOLDERID_Profile) /
             raw.substr(
                 raw.size() > 1 && (raw[1] == L'\\' || raw[1] == L'/') ? 2 : 1);
    wchar_t expanded[32768];
    if (ExpandEnvironmentStringsW(path.c_str(), expanded, 32768) > 0)
      path = expanded;
    std::error_code kind_error;
    pending_file.clear();
    if (fs::is_regular_file(path, kind_error)) {
      pending_file = path;
      path = path.parent_path();
    }
    ++generation;
    preview = review = false;
    directory = path;
    query.clear();
    run(
        [path](Http &http) {
          std::vector<Entry> out;
          std::error_code ec;
          for (fs::directory_iterator
                   it(path, fs::directory_options::skip_permission_denied, ec),
               end;
               it != end; it.increment(ec)) {
            http.check();
            if (ec)
              break;
            auto &e = *it;
            auto name = e.path().filename().wstring();
            if (name.empty() || name.front() == L'.')
              continue;
            auto attr = GetFileAttributesW(e.path().c_str());
            if (attr != INVALID_FILE_ATTRIBUTES &&
                (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
              continue;
            Entry entry;
            entry.path = e.path();
            entry.name = name;
            entry.folder = e.is_directory(ec);
            auto time = e.last_write_time(ec);
            if (!ec)
              entry.date = (std::uint64_t(time.time_since_epoch().count()) ^
                            (1ULL << 63));
            out.push_back(std::move(entry));
          }
          if (ec)
            throw std::runtime_error("Cannot open this folder.");
          sort_entries(out, downloads(path));
          return out;
        },
        [](Panel &p, std::vector<Entry> rows) {
          p.entries = std::move(rows);
          p.filter();
          if (!p.pending_file.empty()) {
            auto path = p.pending_file;
            p.pending_file.clear();
            p.show(path);
          }
        });
  }
  void fetch(bool more = false) {
    if (service == "notes") {
      notes_fetch();
      return;
    }
    if (busy) {
      if (!more)
        reload_requested = true;
      return;
    }
    preview = review = false;
    auto mode = service, q = query, c = cat;
    int offset = more ? next : 0;
    if (!more) {
      ++generation;
      entries.clear();
      visible.clear();
      scroll = 0;
      selection = 0;
    }
    run(
        [mode, q, c, offset](Http &h) {
          return mode == "memes"
                     ? winservices::memes(h, q, c, offset)
                     : Json::parse(h.request("https://bin.aridlin.pl/items"));
        },
        [more](Panel &p, Json page) {
          auto rows = p.meme ? page.at("items") : page;
          if (p.meme) {
            p.categories =
                page.at("categories").get<std::vector<std::string>>();
            p.next = page["next"].is_null() ? -1 : page["next"].get<int>();
          } else
            p.next = -1;
          for (auto &row : rows) {
            Entry e;
            e.name = wide(row.value("name", "file"));
            e.url = media(row.at("url").get<std::string>(), p.meme);
            e.thumb = row.value("thumbnail", "");
            e.kind = row.value("kind", row.value("mime", "file"));
            e.date = row.value("mtime", std::uint64_t(0));
            e.size = row.value("size", std::uint64_t(0));
            e.path = cached_path(e.url, utf8(e.name));
            p.entries.push_back(std::move(e));
          }
          int previous = p.scroll, selection = p.selection;
          p.filter();
          if (more) {
            p.scroll = previous;
            p.selection = selection;
          }
        });
  }
  void get_image(const Entry &e) {
    if (!alive)
      return;
    if (e.kind == "note")
      return;
    if (bitmaps.contains(e.path) || image_pending.contains(e.path) ||
        image_active >= 2 || e.folder)
      return;
    auto self = shared_from_this();
    auto item = e;
    image_pending.insert(e.path);
    ++image_active;
    std::thread([self, item] {
      CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      std::shared_ptr<Bitmap> bitmap;
      try {
        fs::path path = item.path;
        if (!fs::exists(path) && !item.thumb.empty()) {
          Http client;
          if (!self->alive)
            throw std::runtime_error("Closed");
          path = download(client, media(item.thumb, self->meme),
                          "thumbnail.jpg", 8 * 1024 * 1024);
        }
        if (fs::exists(path))
          bitmap = thumbnail(path);
      } catch (...) {
      }
      std::lock_guard lock(self->mutex);
      self->completions.push_back([path = item.path, bitmap](Panel &p) {
        p.image_pending.erase(path);
        --p.image_active;
        if (p.bitmaps.size() >= 64)
          p.bitmaps.erase(p.bitmaps.begin());
        p.bitmaps[path] = bitmap;
      });
      CoUninitialize();
    }).detach();
  }
  void open_index() {
    if (busy || visible.empty())
      return;
    auto item =
        entries[visible[std::clamp(selection, 0, int(visible.size()) - 1)]];
    if (item.kind == "note") {
      selected = item.path;
      preview = true;
      preview_text = note_bodies[item.path];
      return;
    }
    if (item.folder) {
      browse(item.path);
      return;
    }
    if (choosing) {
      selected = item.path;
      review = true;
      preview = false;
      return;
    }
    if (!fs::exists(item.path) && !item.url.empty()) {
      run(
          [item](Http &h) {
            return download(h, item.url, utf8(item.name), 1073741824);
          },
          [](Panel &p, fs::path path) { p.show(path); });
      return;
    }
    show(item.path);
  }
  void show(fs::path path) {
    video.reset();
    animation.reset();
    gif_limited_reported = false;
    selected = path;
    preview = true;
    review = false;
    preview_text.clear();
    text_scroll = 0;
    auto ext = lower(path.extension().wstring());
    if (ext == L".gif" && fs::file_size(path) <= 16 * 1024 * 1024)
      animation = std::make_unique<Gif>(path);
    if (ext == L".mp4" || ext == L".webm" || ext == L".mov" || ext == L".avi" ||
        ext == L".mkv" || ext == L".mp3" || ext == L".wav")
      video = std::make_unique<kalwer::winmedia::Player>(owner, path);
    if (ext == L".txt" || ext == L".md" || ext == L".json" || ext == L".csv" ||
        ext == L".log") {
      std::ifstream in(path, std::ios::binary);
      std::string text(65536, ' ');
      in.read(text.data(), text.size());
      text.resize(size_t(in.gcount()));
      if (text.starts_with("\xef\xbb\xbf"))
        text.erase(0, 3);
      if (text.size() >= 2 &&
          ((unsigned char)text[0] == 255 && (unsigned char)text[1] == 254 ||
           (unsigned char)text[0] == 254 && (unsigned char)text[1] == 255)) {
        bool little = (unsigned char)text[0] == 255;
        for (size_t i = 2; i + 1 < text.size(); i += 2)
          preview_text += wchar_t(
              little
                  ? (unsigned char)text[i] | ((unsigned char)text[i + 1] << 8)
                  : (unsigned char)text[i + 1] | ((unsigned char)text[i] << 8));
      } else {
        preview_text = wide(text);
        if (preview_text.empty() && !text.empty()) {
          int count = MultiByteToWideChar(CP_ACP, 0, text.data(),
                                          int(text.size()), nullptr, 0);
          preview_text.resize(count);
          MultiByteToWideChar(CP_ACP, 0, text.data(), int(text.size()),
                              preview_text.data(), count);
        }
      }
    } else {
      run([path](Http &) { return thumbnail(path, 1280); },
          [path](Panel &p, std::shared_ptr<Bitmap> bitmap) {
            if (p.selected == path)
              p.bitmaps[path] = std::move(bitmap);
          });
    }
  }
  void back() {
    if (incoming && !preview && !review) {
      close();
      return;
    }
    if (preview || review) {
      ++generation;
      if (!shared_paths.empty() && review) {
        incoming = true;
        service.clear();
        meme = false;
      }
      video.reset();
      animation.reset();
      preview = review = false;
      return;
    }
    if (busy)
      return;
    if (choosing) {
      choosing = false;
      query = service_query;
      fetch();
      return;
    }
    if (service.empty()) {
      auto parent = directory.parent_path();
      if (parent != directory)
        browse(parent);
    } else
      close();
  }
  void copy_selected() {
    if (busy)
      return;
    if (service == "notes" && preview) {
      if (OpenClipboard(owner)) {
        EmptyClipboard();
        auto memory = GlobalAlloc(GMEM_MOVEABLE,
                                  (preview_text.size() + 1) * sizeof(wchar_t));
        if (memory) {
          auto ptr = GlobalLock(memory);
          memcpy(ptr, preview_text.c_str(),
                 (preview_text.size() + 1) * sizeof(wchar_t));
          GlobalUnlock(memory);
          if (!SetClipboardData(CF_UNICODETEXT, memory))
            GlobalFree(memory);
        }
        CloseClipboard();
      }
      return;
    }
    if (preview || review) {
      copy(selected);
      return;
    }
    if (visible.empty())
      return;
    auto item = entries[visible[selection]];
    if (!fs::exists(item.path) && !item.url.empty()) {
      run(
          [item](Http &h) {
            return download(h, item.url, utf8(item.name), 1073741824);
          },
          [](Panel &p, fs::path path) {
            copy(path);
            p.message = "Copied.";
          });
    } else {
      copy(item.path);
      message = "Copied.";
    }
  }
  inline Json notes_request(Http &h, const std::string &action,
                            const Json &body = Json()) {
    auto reply = Json::parse(h.request(
        "https://notes.aridlin.pl/api.php?action=" + action,
        body.is_null() ? "GET" : "POST", body.is_null() ? "" : body.dump(),
        body.is_null() ? "" : "Content-Type: application/json\r\n"));
    if (!reply.value("ok", false)) {
      auto error = reply.value("error", "Request failed.");
      if (error == "authentication_required")
        error = "Sign in using Login.";
      throw std::runtime_error(error);
    }
    return reply;
  }
  void notes_fetch(const kalwer::notes::Command &command = {"read", "", true,
                                                            false},
                   const std::string &username = {},
                   const std::string &password = {}) {
    if (busy)
      return;
    ++generation;
    preview = review = false;
    query = command.listing ? command.text : "";
    run(
        [command, username, password](Http &h) {
          if (h.cookies[L"notes.aridlin.pl"].empty())
            h.cookies[L"notes.aridlin.pl"] =
                wide(secret(L"notes-session.dpapi"));
          auto request = [&](const std::string &action,
                             const Json &body = Json()) {
            auto reply = Json::parse(h.request(
                "https://notes.aridlin.pl/api.php?action=" + action,
                body.is_null() ? "GET" : "POST",
                body.is_null() ? "" : body.dump(),
                body.is_null() ? "" : "Content-Type: application/json\r\n"));
            if (!reply.value("ok", false))
              throw std::runtime_error(
                  reply.value("error", "Notes request failed."));
            return reply;
          };
          auto session = request("session");
          auto csrf = session.value("csrf", "");
          if (command.name == "login") {
            request("login", {{"csrf", csrf},
                              {"username", username},
                              {"password", password}});
            secret(L"notes-session.dpapi",
                   utf8(h.cookies[L"notes.aridlin.pl"]));
            session = request("session");
          }
          if (!session.value("authenticated", false))
            throw std::runtime_error("Sign in with Login or *login.");
          if (command.name == "logout") {
            request("logout", {{"csrf", csrf}});
            secret(L"notes-session.dpapi", "");
            return Json::array();
          }
          if (command.save) {
            auto type = command.name == "task"   ? "checklist"
                        : command.name == "idea" ? "project"
                                                 : "markdown";
            auto life = command.name == "soon" || command.name == "someday" ||
                                command.name == "disposable"
                            ? command.name
                            : "reference";
            Json content =
                command.name == "task"
                    ? Json{{"items", Json::array({Json{{"text", command.text},
                                                       {"done", false}}})}}
                : command.name == "idea"
                    ? Json{{"idea", command.text}, {"stage", "captured"}}
                    : Json{{"markdown", command.text}};
            auto saved = request("save-note", {{"csrf", csrf},
                                               {"type", type},
                                               {"lifecycle", life},
                                               {"content", content}});
            if (!saved.contains("note") ||
                saved["note"].value("id", "").empty())
              throw std::runtime_error(
                  "Save not confirmed. Check *read before retrying.");
            return Json::array({saved["note"]});
          }
          auto rows = request("bootstrap").at("notes");
          Json filtered = Json::array();
          for (auto &n : rows) {
            if (n.value("status", "") == "archived" ||
                n.value("status", "") == "abandoned")
              continue;
            bool matches =
                command.name == "read" || command.name == "sync" ||
                command.name == "login" ||
                (command.name == "task" &&
                 n.value("type", "") == "checklist") ||
                (command.name == "idea" && n.value("type", "") == "project") ||
                n.value("lifecycle", "") == command.name;
            if (matches)
              filtered.push_back(n);
          }
          return filtered;
        },
        [saved = command.save,
         signed_out = command.name == "logout"](Panel &p, Json rows) {
          p.entries.clear();
          p.note_bodies.clear();
          for (auto &n : rows) {
            Entry e;
            auto id = n.value("id", "");
            e.path = wide(id);
            e.kind = "note";
            std::string body;
            if (n.contains("content") && n["content"].is_object()) {
              auto &c = n["content"];
              for (auto key : {"markdown", "text", "idea", "next_action",
                               "caption", "filename"})
                if (c.contains(key) && c[key].is_string()) {
                  if (!body.empty())
                    body += "\n\n";
                  body += c[key].get<std::string>();
                }
              if (c.contains("items") && c["items"].is_array())
                for (auto &item : c["items"])
                  body += (item.value("done", false) ? "[x] " : "[ ] ") +
                          item.value("text", "") + "\n";
            }
            e.name = wide(n.value("title", body.substr(0, body.find('\n'))));
            if (e.name.empty())
              e.name = L"Untitled note";
            p.note_bodies[e.path] = wide(body);
            p.entries.push_back(std::move(e));
          }
          p.filter();
          if (p.entries.empty())
            p.message = "No notes.";
          if (saved) {
            p.message = "Saved.";
            if (p.note_saved)
              p.note_saved();
          }
          if (signed_out)
            p.message = "Signed out.";
        });
  }
  void notes_login() {
    if (busy)
      return;
    auto user = prompt(owner, L"Notes username");
    if (!user)
      return;
    auto pass = prompt(owner, L"Notes password", {}, true);
    if (!pass)
      return;
    notes_fetch({"login", ""}, utf8(*user), utf8(*pass));
  }

  void upload() {
    if (busy || selected.empty())
      return;
    auto file = selected;
    auto files =
        shared_paths.empty() ? std::vector<fs::path>{file} : shared_paths;
    auto c = cat == "all" ? (categories.empty() ? "image" : categories.front())
                          : cat;
    auto name = utf8(file.stem().wstring());
    if (meme) {
      auto title = prompt(owner, L"Name", file.stem().wstring());
      if (!title)
        return;
      name = utf8(*title);
    }
    auto m = meme;
    run(
        [file, files, c, name, m](Http &h) {
          if (!m)
            return upload_bin(h, files);
          std::string links;
          for (auto &path : files) {
            if (!links.empty())
              links += '\n';
            links += upload_meme(
                h, path, c,
                files.size() == 1 ? name : utf8(path.stem().wstring()));
          }
          return links;
        },
        [](Panel &p, std::string link) {
          if (OpenClipboard(p.owner)) {
            EmptyClipboard();
            auto value = wide(link);
            auto memory = GlobalAlloc(GMEM_MOVEABLE,
                                      (value.size() + 1) * sizeof(wchar_t));
            if (memory) {
              auto bytes = GlobalLock(memory);
              memcpy(bytes, value.c_str(),
                     (value.size() + 1) * sizeof(wchar_t));
              GlobalUnlock(memory);
              if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
            }
            CloseClipboard();
          }
          p.choosing = p.review = false;
          p.shared_paths.clear();
          p.query.clear();
          p.fetch();
          p.message = "Uploaded. Link copied.";
        });
    message = "Uploading…";
  }
  void places_menu() {
    if (busy)
      return;
    auto menu = CreatePopupMenu();
    for (size_t i = 0; i < bookmarks.size(); i++)
      AppendMenuW(menu, MF_STRING, UINT_PTR(i + 1),
                  bookmarks[i].wstring().c_str());
    POINT point{};
    GetCursorPos(&point);
    auto id = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x,
                               point.y, owner, nullptr);
    DestroyMenu(menu);
    if (id > 0 && id <= bookmarks.size())
      browse(bookmarks[id - 1]);
  }
  void category_menu() {
    if (busy)
      return;
    auto menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"All categories");
    for (size_t i = 0; i < categories.size(); i++)
      AppendMenuW(menu, MF_STRING, UINT_PTR(i + 2),
                  wide(categories[i]).c_str());
    POINT point{};
    GetCursorPos(&point);
    auto id = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x,
                               point.y, owner, nullptr);
    DestroyMenu(menu);
    if (!id)
      return;
    cat = id == 1 ? "all" : categories[id - 2];
    if (!review)
      fetch();
  }
  void change_query() {
    auto value = prompt(owner, L"Search", wide(query));
    if (!value)
      return;
    query = utf8(*value);
    if (service.empty() || choosing || service == "notes")
      filter();
    else
      fetch();
  }
  void key() {
    auto value = prompt(owner, L"Private Memes upload key", {}, true);
    if (!value)
      return;
    auto text = utf8(*value);
    if (!std::regex_match(text, std::regex("[a-f0-9]{64}"))) {
      message = "Enter a valid 64-character key.";
      return;
    }
    secret(L"memes-upload-key.dpapi", text);
    message = "Key saved.";
  }
  void click(float x, float y, bool twice) {
    try {
      for (auto &h : hits)
        if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) {
          pressed = int(&h - hits.data());
          return;
        }
      if (preview || review)
        return;
      if (x < 0 || x > 440 || y < 112 || y >= 472)
        return;
      int index = scroll + int((y - 112) / 108) * 4 + int(x / 110);
      if (index >= 0 && index < int(visible.size())) {
        selection = index;
        if (twice)
          open_index();
      }
    } catch (const std::exception &e) {
      message = e.what();
    }
  }
  void release(float x, float y) {
    if (pressed < 0)
      return;
    auto index = pressed;
    pressed = -1;
    if (index >= int(hits.size()))
      return;
    auto hit = hits[index];
    if (x >= hit.x && x < hit.x + hit.w && y >= hit.y && y < hit.y + hit.h)
      try {
        hit.action();
      } catch (const std::exception &e) {
        message = e.what();
      }
  }
  void hover(float x, float y) {
    hovered = -1;
    for (size_t i = 0; i < hits.size(); i++) {
      auto &h = hits[i];
      if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) {
        hovered = int(i);
        break;
      }
    }
  }
  void wheel(int delta) {
    if (preview && !preview_text.empty()) {
      text_scroll = std::clamp(text_scroll + (delta < 0 ? 3 : -3), 0,
                               std::max(0, text_lines - 18));
      return;
    }
    if (preview || review)
      return;
    scroll = std::clamp(scroll + (delta < 0 ? 4 : -4), 0,
                        std::max(0, ((int(visible.size()) - 1) / 4 - 2) * 4));
    if (service.size() && !choosing && delta < 0 &&
        scroll + 16 >= int(visible.size()) && next >= 0)
      fetch(true);
  }
  bool keydown(UINT key, bool control) {
    try {
      if (key == VK_ESCAPE) {
        if (preview || review || choosing)
          back();
        else
          close();
      } else if (control && key == 'C')
        copy_selected();
      else if (control && key == 'L' && (service.empty() || choosing)) {
        auto value = prompt(owner, L"Folder", directory.wstring());
        if (value)
          browse(*value);
      } else if (control && (key == 'F' || key == 'L'))
        change_query();
      else if (key == VK_BACK)
        back();
      else if (key == VK_SPACE && preview) {
        if (video)
          video->toggle();
        else if (animation)
          animation->data->paused = !animation->data->paused;
      } else if (key == VK_RETURN && !preview && !review)
        open_index();
      else if (key == VK_RETURN && review)
        upload();
      else if (key == VK_F5) {
        if (service.empty() || choosing)
          browse(directory);
        else
          fetch();
      } else if (!visible.empty() && !preview && !review) {
        int step = key == VK_LEFT    ? -1
                   : key == VK_RIGHT ? 1
                   : key == VK_UP    ? -4
                   : key == VK_DOWN  ? 4
                   : key == VK_NEXT  ? 12
                   : key == VK_PRIOR ? -12
                                     : 0;
        selection = std::clamp(selection + step, 0, int(visible.size()) - 1);
        if (selection < scroll)
          scroll = (selection / 4) * 4;
        if (selection >= scroll + 12)
          scroll = (selection / 4 - 2) * 4;
      }
      return true;
    } catch (const std::exception &e) {
      message = e.what();
      return true;
    }
  }
};
} // namespace kalwer::winfiles
