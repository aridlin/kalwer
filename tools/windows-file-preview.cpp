#define wWinMain kalwer_application_entry
#include "../windows/kalwer_windows.cpp"
#undef wWinMain
#include <iostream>
void check(HRESULT value, const char *where) {
  if (FAILED(value)) {
    std::cerr << where << ": " << std::hex << value << "\n";
    throw std::runtime_error(where);
  }
}
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  check(OleInitialize(nullptr), "COM");
  state.instance = GetModuleHandleW(nullptr);
  state.render.scale = 1;
  check(create_device_independent_resources(), "Fonts");
  auto &r = state.render;
  D3D_FEATURE_LEVEL level;
  HRESULT device = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr,
                                     0, D3D11_SDK_VERSION, r.d3d.GetAddressOf(),
                                     &level, r.d3d_context.GetAddressOf());
  if (FAILED(device))
    device = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, r.d3d.GetAddressOf(), &level,
                               r.d3d_context.GetAddressOf());
  check(device, "D3D");
  ComPtr<IDXGIDevice> dxgi;
  check(r.d3d.As(&dxgi), "DXGI");
  check(r.d2d_factory->CreateDevice(dxgi.Get(), r.d2d_device.GetAddressOf()),
        "D2D device");
  check(r.d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                          r.d2d_context.GetAddressOf()),
        "D2D context");
  check(r.d2d_context->CreateSolidColorBrush(color(1, 1, 1),
                                             r.brush.GetAddressOf()),
        "Brush");
  auto properties =
      D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                D2D1_ALPHA_MODE_PREMULTIPLIED));
  ComPtr<ID2D1Bitmap1> target;
  check(r.d2d_context->CreateBitmap(D2D1::SizeU(480, 540), nullptr, 0,
                                    &properties, target.GetAddressOf()),
        "Target");
  r.d2d_context->SetTarget(target.Get());
  auto p = std::make_shared<kalwer::winfiles::Panel>();
  state.popup_browser = p;
  p->load_places();
  p->browse(std::filesystem::path(wide(argv[1])));
  auto deadline = GetTickCount64() + 15000;
  while (p->busy && GetTickCount64() < deadline) {
    p->poll();
    Sleep(10);
  }
  if (p->busy || p->entries.empty())
    return 3;
  auto draw = [&] {
    r.d2d_context->BeginDraw();
    r.d2d_context->Clear(color(.01f, .06f, .04f, .88f));
    draw_file_panel(12, 12);
    check(r.d2d_context->EndDraw(), "Draw");
  };
  for (int i = 0; i < 30; i++) {
    p->poll();
    draw();
    Sleep(30);
  }
  std::cout << "Cached previews: " << p->bitmaps.size() << "\n";
  if (argc > 3 && std::string(argv[3]) == "preview") {
    for (size_t i = 0; i < p->visible.size(); i++)
      if (p->entries[p->visible[i]].name == L"example.png")
        p->selection = int(i);
    p->open_index();
    while (p->busy && GetTickCount64() < deadline) {
      p->poll();
      Sleep(10);
    }
    draw();
  }
  auto cpu = properties;
  cpu.bitmapOptions =
      D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
  ComPtr<ID2D1Bitmap1> copy;
  check(r.d2d_context->CreateBitmap(D2D1::SizeU(480, 540), nullptr, 0, &cpu,
                                    copy.GetAddressOf()),
        "CPU bitmap");
  check(copy->CopyFromBitmap(nullptr, target.Get(), nullptr), "Readback");
  D2D1_MAPPED_RECT mapped;
  check(copy->Map(D2D1_MAP_OPTIONS_READ, &mapped), "Map");
  ComPtr<IWICBitmap> pixels;
  check(r.wic_factory->CreateBitmapFromMemory(
            480, 540, GUID_WICPixelFormat32bppPBGRA, mapped.pitch,
            mapped.pitch * 540, mapped.bits, pixels.GetAddressOf()),
        "WIC image");
  ComPtr<IWICStream> stream;
  check(r.wic_factory->CreateStream(stream.GetAddressOf()), "Stream");
  check(stream->InitializeFromFilename(wide(argv[2]).c_str(), GENERIC_WRITE),
        "Output");
  ComPtr<IWICBitmapEncoder> encoder;
  check(r.wic_factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                     encoder.GetAddressOf()),
        "PNG");
  check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Encoder");
  ComPtr<IWICBitmapFrameEncode> frame;
  check(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr), "Frame");
  check(frame->Initialize(nullptr), "Frame init");
  check(frame->WriteSource(pixels.Get(), nullptr), "Write");
  check(frame->Commit(), "Frame commit");
  check(encoder->Commit(), "Commit");
  copy->Unmap();
  p->shutdown();
  state.popup_browser.reset();
  app_search.stop();
  icon_worker.stop();
  std::cout << "Windows native file panel rendered.\n";
  return 0;
}
