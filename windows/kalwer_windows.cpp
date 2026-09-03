#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <knownfolders.h>
#include <shellscalingapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kWindowClass[] = L"KalwerWindowsHost";
constexpr wchar_t kWindowTitle[] = L"Kalwer";
constexpr int kLogicalWidth = 650;
constexpr int kLogicalHeight = 632;
constexpr float kSearchX = 15.0f;
constexpr float kSearchY = 12.0f;
constexpr float kSearchWidth = 620.0f;
constexpr float kSearchHeight = 66.0f;
constexpr float kResultsY = 88.0f;
constexpr float kResultX = 25.0f;
constexpr float kResultWidth = 600.0f;
constexpr float kRowHeight = 58.0f;
constexpr float kRowPitch = 66.0f;
constexpr int kVisibleResults = 8;
constexpr int kSelectableResults = 5;
constexpr UINT kHotkeyId = 1;
constexpr UINT kTimerId = 1;
constexpr UINT kToggleMessage = WM_APP + 41;

constexpr D2D1_COLOR_F color(float red, float green, float blue, float alpha = 1.0f) {
    return D2D1_COLOR_F{red, green, blue, alpha};
}

std::wstring lower_copy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring trim_copy(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}

std::filesystem::path local_data_directory() {
    PWSTR raw = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                       nullptr, &raw))) {
        result = raw;
        CoTaskMemFree(raw);
    }
    if (result.empty()) result = L".";
    result /= L"Kalwer";
    return result;
}

struct AppEntry {
    std::wstring title;
    std::wstring subtitle;
    std::filesystem::path link;
    std::wstring folded;
    std::vector<int> matches;
    int score = 0;
    bool pinned = false;
};

struct RenderDevice {
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> d3d_context;
    ComPtr<IDXGISwapChain1> swap_chain;
    ComPtr<ID3D11Texture2D> ui_texture;
    ComPtr<ID3D11ShaderResourceView> ui_view;
    ComPtr<ID3D11RenderTargetView> back_view;
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID2D1Factory1> d2d_factory;
    ComPtr<ID2D1Device> d2d_device;
    ComPtr<ID2D1DeviceContext> d2d_context;
    ComPtr<ID2D1Bitmap1> ui_target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteFactory> write_factory;
    ComPtr<IDWriteTextFormat> title_format;
    ComPtr<IDWriteTextFormat> subtitle_format;
    ComPtr<IDWriteTextFormat> input_format;
    ComPtr<IDWriteTextFormat> tiny_format;
    ComPtr<IWICImagingFactory> wic_factory;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionTarget> composition_target;
    ComPtr<IDCompositionVisual> composition_visual;
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap1>> icon_cache;
    UINT pixel_width = 0;
    UINT pixel_height = 0;
    float scale = 1.0f;
    bool direct_composition = true;

    void reset_size_resources() {
        if (d2d_context) d2d_context->SetTarget(nullptr);
        ui_target.Reset();
        ui_view.Reset();
        ui_texture.Reset();
        back_view.Reset();
        swap_chain.Reset();
        composition_visual.Reset();
        composition_target.Reset();
        composition.Reset();
        icon_cache.clear();
    }
};

struct State {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HWND edit = nullptr;
    WNDPROC edit_proc = nullptr;
    HANDLE mutex = nullptr;
    RenderDevice render;
    std::vector<AppEntry> all_apps;
    std::vector<AppEntry> results;
    std::vector<std::wstring> favorites;
    int selection = 0;
    int scroll_offset = 0;
    float selection_visual = 0.0f;
    bool visible = false;
    bool opening = false;
    bool render_dirty = true;
    bool hotkey_registered = false;
    std::wstring graphics_stage;
    std::chrono::steady_clock::time_point opened_at{};
    std::chrono::steady_clock::time_point hidden_at{};
    std::wstring restored_query;
    int restored_selection = 0;
    int restored_scroll = 0;
};

State state;

void fail_message(const wchar_t* text, HRESULT code = S_OK) {
    std::wostringstream stream;
    stream << text;
    if (FAILED(code)) stream << L"\nHRESULT 0x" << std::hex << static_cast<unsigned long>(code);
    MessageBoxW(nullptr, stream.str().c_str(), L"Kalwer Windows", MB_OK | MB_ICONERROR);
}

std::wstring window_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length), L'\0');
    if (length) GetWindowTextW(control, text.data(), length + 1);
    return text;
}

void set_window_text_preserving_end(HWND control, const std::wstring& text) {
    SetWindowTextW(control, text.c_str());
    SendMessageW(control, EM_SETSEL, text.size(), text.size());
}

std::optional<std::pair<int, std::vector<int>>> fuzzy_match(const std::wstring& query,
                                                             const std::wstring& candidate) {
    if (query.empty()) return std::pair<int, std::vector<int>>{0, {}};
    std::vector<int> positions;
    size_t cursor = 0;
    int score = 0;
    int previous = -2;
    for (wchar_t wanted : query) {
        const size_t found = candidate.find(wanted, cursor);
        if (found == std::wstring::npos) return std::nullopt;
        const int position = static_cast<int>(found);
        positions.push_back(position);
        score += position == previous + 1 ? 18 : 4;
        if (position == 0 || candidate[static_cast<size_t>(position - 1)] == L' ' ||
            candidate[static_cast<size_t>(position - 1)] == L'-' ||
            candidate[static_cast<size_t>(position - 1)] == L'_') score += 15;
        score -= position;
        previous = position;
        cursor = found + 1;
    }
    score -= static_cast<int>(candidate.size() - query.size()) / 3;
    return std::pair<int, std::vector<int>>{score, std::move(positions)};
}

void enumerate_programs_at(const std::filesystem::path& root, std::vector<AppEntry>& output) {
    std::error_code error;
    if (!std::filesystem::exists(root, error)) return;
    for (std::filesystem::recursive_directory_iterator iterator(
             root, std::filesystem::directory_options::skip_permission_denied, error), end;
         iterator != end; iterator.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (!iterator->is_regular_file(error)) continue;
        const std::filesystem::path path = iterator->path();
        if (lower_copy(path.extension().wstring()) != L".lnk") continue;
        AppEntry entry;
        entry.title = path.stem().wstring();
        entry.subtitle = path.parent_path().filename().wstring();
        entry.link = path;
        entry.folded = lower_copy(entry.title + L" " + entry.subtitle);
        output.push_back(std::move(entry));
    }
}

void load_apps() {
    std::vector<AppEntry> apps;
    for (REFKNOWNFOLDERID id : {FOLDERID_Programs, FOLDERID_CommonPrograms}) {
        PWSTR raw = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw))) {
            enumerate_programs_at(raw, apps);
            CoTaskMemFree(raw);
        }
    }
    std::sort(apps.begin(), apps.end(), [](const AppEntry& left, const AppEntry& right) {
        return lower_copy(left.title) < lower_copy(right.title);
    });
    apps.erase(std::unique(apps.begin(), apps.end(), [](const AppEntry& left,
                                                        const AppEntry& right) {
        return lower_copy(left.title) == lower_copy(right.title);
    }), apps.end());
    state.all_apps = std::move(apps);
}

void load_favorites() {
    std::ifstream input(local_data_directory() / L"favorites-v1.txt", std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) state.favorites.push_back(wide(line));
    }
}

void save_favorites() {
    const auto directory = local_data_directory();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    std::ofstream output(directory / L"favorites-v1.txt", std::ios::binary | std::ios::trunc);
    for (const auto& favorite : state.favorites) output << utf8(favorite) << '\n';
}

bool is_favorite(const AppEntry& entry) {
    const std::wstring key = lower_copy(entry.link.wstring());
    return std::find(state.favorites.begin(), state.favorites.end(), key) != state.favorites.end();
}

class ExpressionParser {
public:
    explicit ExpressionParser(const std::wstring& source) : source_(source) {}

    bool parse(double& value) {
        position_ = 0;
        valid_ = true;
        saw_operator_ = false;
        value = expression();
        whitespace();
        return valid_ && position_ == source_.size() && saw_operator_ && std::isfinite(value);
    }

private:
    void whitespace() {
        while (position_ < source_.size() && std::iswspace(source_[position_])) ++position_;
    }
    bool take(wchar_t character) {
        whitespace();
        if (position_ >= source_.size() || source_[position_] != character) return false;
        ++position_;
        return true;
    }
    double expression() {
        double value = term();
        while (valid_) {
            if (take(L'+')) { saw_operator_ = true; value += term(); }
            else if (take(L'-')) { saw_operator_ = true; value -= term(); }
            else break;
        }
        return value;
    }
    double term() {
        double value = unary();
        while (valid_) {
            if (take(L'*')) { saw_operator_ = true; value *= unary(); }
            else if (take(L'/')) {
                saw_operator_ = true;
                const double divisor = unary();
                if (std::abs(divisor) < 1e-15) valid_ = false;
                else value /= divisor;
            } else if (take(L'%')) {
                saw_operator_ = true;
                value = (value / 100.0) * unary();
            } else break;
        }
        return value;
    }
    double unary() {
        if (take(L'+')) return unary();
        if (take(L'-')) return -unary();
        return power();
    }
    double power() {
        double value = primary();
        if (take(L'^')) { saw_operator_ = true; value = std::pow(value, unary()); }
        return value;
    }
    double primary() {
        whitespace();
        bool square_root = false;
        if (source_.compare(position_, 4, L"sqrt") == 0) {
            position_ += 4;
            square_root = true;
        } else if (position_ < source_.size() && source_[position_] == L'√') {
            ++position_;
            square_root = true;
        }
        if (square_root) {
            saw_operator_ = true;
            const bool parenthesized = take(L'(');
            const double value = parenthesized ? expression() : unary();
            if (parenthesized && !take(L')')) valid_ = false;
            if (value < 0.0) { valid_ = false; return 0.0; }
            return std::sqrt(value);
        }
        if (take(L'(')) {
            const double value = expression();
            if (!take(L')')) valid_ = false;
            return value;
        }
        whitespace();
        const wchar_t* start = source_.c_str() + position_;
        wchar_t* end = nullptr;
        const double value = std::wcstod(start, &end);
        if (!end || end == start) { valid_ = false; return 0.0; }
        position_ += static_cast<size_t>(end - start);
        return value;
    }

    const std::wstring& source_;
    size_t position_ = 0;
    bool valid_ = true;
    bool saw_operator_ = false;
};

std::wstring format_number(double value) {
    if (std::abs(value) < 5e-14) value = 0.0;
    std::wostringstream stream;
    stream << std::setprecision(12) << std::defaultfloat << value;
    return stream.str();
}

std::optional<AppEntry> calculator_result(const std::wstring& query) {
    double value = 0.0;
    ExpressionParser parser(query);
    if (!parser.parse(value)) return std::nullopt;
    AppEntry result;
    result.title = format_number(value);
    result.subtitle = L"CALCULATED RESULT · ENTER TO COPY";
    result.link = L"::calculator";
    result.folded = lower_copy(result.title);
    return result;
}

void update_results() {
    const std::wstring raw_query = window_text(state.edit);
    const std::wstring query = lower_copy(trim_copy(raw_query));
    std::vector<AppEntry> filtered;

    if (const auto calculation = calculator_result(query)) {
        filtered.push_back(*calculation);
    } else if (!query.empty() && query.front() == L'?') {
        AppEntry google;
        google.title = L"Search Google";
        google.subtitle = trim_copy(raw_query.substr(1));
        google.link = L"::google";
        filtered.push_back(std::move(google));
    } else if (!query.empty() && query.front() == L'>') {
        AppEntry command;
        command.title = L"Run in terminal";
        command.subtitle = trim_copy(raw_query.substr(1));
        command.link = L"::command";
        filtered.push_back(std::move(command));
    } else {
        for (const auto& source : state.all_apps) {
            AppEntry result = source;
            result.pinned = is_favorite(result);
            const auto match = fuzzy_match(query, result.folded);
            if (!match) continue;
            result.score = match->first;
            result.matches = match->second;
            filtered.push_back(std::move(result));
        }
        std::stable_sort(filtered.begin(), filtered.end(), [](const AppEntry& left,
                                                              const AppEntry& right) {
            if (left.pinned != right.pinned) return left.pinned;
            if (left.score != right.score) return left.score > right.score;
            return lower_copy(left.title) < lower_copy(right.title);
        });
    }
    state.results = std::move(filtered);
    state.selection = 0;
    state.scroll_offset = 0;
    state.selection_visual = 0.0f;
    state.render_dirty = true;
}

void move_selection(int delta) {
    if (state.results.empty()) return;
    state.selection = std::clamp(state.selection + delta, 0,
                                 static_cast<int>(state.results.size()) - 1);
    if (state.selection < state.scroll_offset) state.scroll_offset = state.selection;
    if (state.selection >= state.scroll_offset + kSelectableResults) {
        state.scroll_offset = state.selection - kSelectableResults + 1;
    }
    const int maximum_offset = std::max(0, static_cast<int>(state.results.size()) -
                                           kVisibleResults);
    state.scroll_offset = std::clamp(state.scroll_offset, 0, maximum_offset);
    state.render_dirty = true;
}

std::wstring url_encode(const std::wstring& source) {
    const std::string bytes = utf8(source);
    std::ostringstream output;
    output << std::uppercase << std::hex;
    for (unsigned char byte : bytes) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
            byte == '.' || byte == '~') output << static_cast<char>(byte);
        else if (byte == ' ') output << '+';
        else output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return wide(output.str());
}

void hide_launcher();

void activate_selection() {
    if (state.results.empty()) return;
    const AppEntry& result = state.results[static_cast<size_t>(state.selection)];
    if (result.link == L"::calculator") {
        if (OpenClipboard(state.window)) {
            EmptyClipboard();
            const size_t bytes = (result.title.size() + 1) * sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (memory) {
                void* destination = GlobalLock(memory);
                std::memcpy(destination, result.title.c_str(), bytes);
                GlobalUnlock(memory);
                SetClipboardData(CF_UNICODETEXT, memory);
            }
            CloseClipboard();
        }
    } else if (result.link == L"::google") {
        const std::wstring url = L"https://www.google.com/search?q=" +
                                 url_encode(result.subtitle);
        ShellExecuteW(state.window, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (result.link == L"::command") {
        const std::wstring arguments = L"-w 0 new-tab --title Kalwer pwsh.exe -NoExit -Command \"" +
                                       result.subtitle + L"\"";
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(state.window, L"open", L"wt.exe",
                                                    arguments.c_str(), nullptr,
                                                    SW_SHOWNORMAL)) <= 32) {
            ShellExecuteW(state.window, L"open", L"cmd.exe",
                          (L"/K " + result.subtitle).c_str(), nullptr, SW_SHOWNORMAL);
        }
    } else {
        ShellExecuteW(state.window, L"open", result.link.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    }
    hide_launcher();
}

void toggle_favorite() {
    if (state.results.empty()) return;
    const AppEntry& result = state.results[static_cast<size_t>(state.selection)];
    if (result.link.wstring().rfind(L"::", 0) == 0) return;
    const std::wstring key = lower_copy(result.link.wstring());
    const auto found = std::find(state.favorites.begin(), state.favorites.end(), key);
    if (found == state.favorites.end()) state.favorites.push_back(key);
    else state.favorites.erase(found);
    save_favorites();
    const std::wstring query = window_text(state.edit);
    update_results();
    set_window_text_preserving_end(state.edit, query);
}

HRESULT compile_shader(const char* source, const char* entry, const char* target,
                       ID3DBlob** output) {
    ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(source, std::strlen(source), "kalwer-mask", nullptr,
                                     nullptr, entry, target,
                                     D3DCOMPILE_ENABLE_STRICTNESS |
                                         D3DCOMPILE_OPTIMIZATION_LEVEL3,
                                     0, output, errors.GetAddressOf());
    if (FAILED(result) && errors) {
        OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
    }
    return result;
}

const char* kVertexShader = R"hlsl(
struct Output { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Output main(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    Output output;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = uv;
    return output;
})hlsl";

const char* kPixelShader = R"hlsl(
Texture2D ui_texture : register(t0);
SamplerState ui_sampler : register(s0);
cbuffer Parameters : register(b0) {
    float2 logical_size;
    float opening;
    float selection_y;
    int has_results;
    float3 padding;
};

static const float bayer[64] = {
     0,48,12,60, 3,51,15,63, 32,16,44,28,35,19,47,31,
     8,56, 4,52,11,59, 7,55, 40,24,36,20,43,27,39,23,
     2,50,14,62, 1,49,13,61, 34,18,46,30,33,17,45,29,
    10,58, 6,54, 9,57, 5,53, 42,26,38,22,41,25,37,21
};

float ease_out_cubic(float value) {
    float inverse = 1.0 - saturate(value);
    return 1.0 - inverse * inverse * inverse;
}

float rounded_box(float2 value, float2 center, float2 half_size, float radius) {
    float2 q = abs(value - center) - half_size + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float4 ui = ui_texture.SampleLevel(ui_sampler, uv, 0);
    float2 pixel_position = uv * logical_size;

    if (has_results != 0) {
        float2 selection_center = float2(325.0, selection_y + 29.0);
        float box_distance = rounded_box(pixel_position, selection_center,
                                         float2(301.5, 30.5), 11.5);
        float inside = 1.0 - smoothstep(-0.2, 1.0, box_distance);
        float outline = 1.0 - smoothstep(0.75, 1.65, abs(box_distance));
        float4 fill = float4(0.31, 0.68, 0.47, 0.07 * inside);
        fill.rgb *= fill.a;
        ui = fill + ui * (1.0 - fill.a);
        float4 border = float4(0.46, 0.82, 0.57, 0.94 * outline);
        border.rgb *= border.a;
        ui = border + ui * (1.0 - border.a);
    }
    const float pitch = 8.0;
    float row = floor(pixel_position.y / pitch);
    float offset = fmod(row, 2.0) * pitch * 0.5;
    float column = floor((pixel_position.x - offset) / pitch);
    float2 center = float2((column + 0.5) * pitch + offset, (row + 0.5) * pitch);

    const float curve_origin = 286.0;
    const float halftone_start = 418.0;
    float depth = saturate((pixel_position.y - curve_origin) /
                           (logical_size.y - curve_origin));
    float tapered_depth = smoothstep(0.0, 1.0, depth);
    float radius = lerp(5.80, 0.42, pow(tapered_depth, 0.92));
    float density_depth = smoothstep(0.28, 1.0, depth);
    float density = 1.0 - 0.16 * pow(density_depth, 1.65);
    int bx = ((int)column % 8 + 8) % 8;
    int by = ((int)row % 8 + 8) % 8;
    float threshold = (bayer[by * 8 + bx] + 0.5) / 64.0;
    float site_visibility = density >= 0.999
        ? 1.0
        : 1.0 - smoothstep(density - 0.035, density + 0.035, threshold);
    float site_radius = radius * sqrt(max(site_visibility, 0.0));
    float antialias = max(fwidth(length(pixel_position - center)), 0.65);
    float dot = site_visibility < 0.01
        ? 0.0
        : 1.0 - smoothstep(site_radius - antialias, site_radius + antialias,
                           length(pixel_position - center));
    float coverage = pixel_position.y < halftone_start ? 1.0 : dot;
    float reveal_front = 88.0 + ease_out_cubic(opening) * (logical_size.y - 86.0);
    float reveal = pixel_position.y < 88.0
        ? 1.0
        : 1.0 - smoothstep(reveal_front - 2.0, reveal_front + 2.0, pixel_position.y);
    return ui * (coverage * reveal);
})hlsl";

HRESULT create_device_independent_resources() {
    auto& render = state.render;
    D2D1_FACTORY_OPTIONS options{};
#ifdef _DEBUG
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       __uuidof(ID2D1Factory1), &options,
                                       reinterpret_cast<void**>(render.d2d_factory.GetAddressOf()));
    if (FAILED(result)) return result;
    result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(render.write_factory.GetAddressOf()));
    if (FAILED(result)) return result;
    result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(render.wic_factory.GetAddressOf()));
    if (FAILED(result)) return result;

    const wchar_t* family = L"JetBrainsMono Nerd Font";
    result = render.write_factory->CreateTextFormat(
        family, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"en-US", render.input_format.GetAddressOf());
    if (FAILED(result)) return result;
    result = render.write_factory->CreateTextFormat(
        family, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 15.33f, L"en-US", render.title_format.GetAddressOf());
    if (FAILED(result)) return result;
    result = render.write_factory->CreateTextFormat(
        family, nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 10.67f, L"en-US", render.subtitle_format.GetAddressOf());
    if (FAILED(result)) return result;
    result = render.write_factory->CreateTextFormat(
        family, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-US", render.tiny_format.GetAddressOf());
    return result;
}

HRESULT create_graphics_device() {
    auto& render = state.render;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL actual_level{};
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                       levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                       render.d3d.GetAddressOf(), &actual_level,
                                       render.d3d_context.GetAddressOf());
    if (result == E_INVALIDARG) {
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   levels + 1, ARRAYSIZE(levels) - 1, D3D11_SDK_VERSION,
                                   render.d3d.GetAddressOf(), &actual_level,
                                   render.d3d_context.GetAddressOf());
    }
    if (FAILED(result)) return result;

    ComPtr<IDXGIDevice> dxgi_device;
    result = render.d3d.As(&dxgi_device);
    if (FAILED(result)) return result;
    result = render.d2d_factory->CreateDevice(dxgi_device.Get(), render.d2d_device.GetAddressOf());
    if (FAILED(result)) return result;
    result = render.d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                     render.d2d_context.GetAddressOf());
    if (FAILED(result)) return result;
    render.d2d_context->SetUnitMode(D2D1_UNIT_MODE_DIPS);
    result = render.d2d_context->CreateSolidColorBrush(color(1, 1, 1),
                                                       render.brush.GetAddressOf());
    if (FAILED(result)) return result;

    ComPtr<ID3DBlob> vertex_blob;
    result = compile_shader(kVertexShader, "main", "vs_4_0", vertex_blob.GetAddressOf());
    if (FAILED(result)) return result;
    result = render.d3d->CreateVertexShader(vertex_blob->GetBufferPointer(),
                                             vertex_blob->GetBufferSize(), nullptr,
                                             render.vertex_shader.GetAddressOf());
    if (FAILED(result)) return result;
    ComPtr<ID3DBlob> pixel_blob;
    result = compile_shader(kPixelShader, "main", "ps_4_0", pixel_blob.GetAddressOf());
    if (FAILED(result)) return result;
    result = render.d3d->CreatePixelShader(pixel_blob->GetBufferPointer(),
                                            pixel_blob->GetBufferSize(), nullptr,
                                            render.pixel_shader.GetAddressOf());
    if (FAILED(result)) return result;

    D3D11_BUFFER_DESC constant_description{};
    constant_description.ByteWidth = 32;
    constant_description.Usage = D3D11_USAGE_DYNAMIC;
    constant_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constant_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    result = render.d3d->CreateBuffer(&constant_description, nullptr,
                                      render.constants.GetAddressOf());
    if (FAILED(result)) return result;
    D3D11_SAMPLER_DESC sampler_description{};
    sampler_description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_description.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_description.MaxLOD = D3D11_FLOAT32_MAX;
    return render.d3d->CreateSamplerState(&sampler_description,
                                           render.sampler.GetAddressOf());
}

HRESULT create_size_resources(UINT pixel_width, UINT pixel_height, float scale) {
    auto& render = state.render;
    render.reset_size_resources();
    render.pixel_width = pixel_width;
    render.pixel_height = pixel_height;
    render.scale = scale;

    ComPtr<IDXGIDevice> dxgi_device;
    HRESULT result = render.d3d.As(&dxgi_device);
    if (FAILED(result)) { state.graphics_stage = L"Query IDXGI device"; return result; }
    ComPtr<IDXGIAdapter> adapter;
    result = dxgi_device->GetAdapter(adapter.GetAddressOf());
    if (FAILED(result)) { state.graphics_stage = L"Get DXGI adapter"; return result; }
    ComPtr<IDXGIFactory2> factory;
    result = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(result)) { state.graphics_stage = L"Get DXGI factory"; return result; }

    DXGI_SWAP_CHAIN_DESC1 swap_description{};
    swap_description.Width = pixel_width;
    swap_description.Height = pixel_height;
    swap_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_description.SampleDesc.Count = 1;
    swap_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_description.BufferCount = 2;
    swap_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swap_description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    result = factory->CreateSwapChainForComposition(render.d3d.Get(), &swap_description,
                                                     nullptr, render.swap_chain.GetAddressOf());
    render.direct_composition = SUCCEEDED(result);
    if (FAILED(result) && result == E_NOTIMPL) {
        // Wine and some remote/legacy sessions do not expose DirectComposition.
        // Keep the real Windows path GPU-composited and use an opaque HWND swap
        // chain only as a compatibility/test fallback.
        swap_description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        result = factory->CreateSwapChainForHwnd(render.d3d.Get(), state.window,
                                                  &swap_description, nullptr, nullptr,
                                                  render.swap_chain.GetAddressOf());
    }
    if (FAILED(result)) { state.graphics_stage = L"Create composition swap chain"; return result; }

    ComPtr<ID3D11Texture2D> back_buffer;
    result = render.swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf()));
    if (FAILED(result)) { state.graphics_stage = L"Get swap-chain buffer"; return result; }
    result = render.d3d->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                                render.back_view.GetAddressOf());
    if (FAILED(result)) { state.graphics_stage = L"Create back-buffer view"; return result; }

    D3D11_TEXTURE2D_DESC ui_description{};
    ui_description.Width = pixel_width;
    ui_description.Height = pixel_height;
    ui_description.MipLevels = 1;
    ui_description.ArraySize = 1;
    ui_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    ui_description.SampleDesc.Count = 1;
    ui_description.Usage = D3D11_USAGE_DEFAULT;
    ui_description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    result = render.d3d->CreateTexture2D(&ui_description, nullptr,
                                         render.ui_texture.GetAddressOf());
    if (FAILED(result)) { state.graphics_stage = L"Create finished-UI texture"; return result; }
    result = render.d3d->CreateShaderResourceView(render.ui_texture.Get(), nullptr,
                                                  render.ui_view.GetAddressOf());
    if (FAILED(result)) { state.graphics_stage = L"Create finished-UI shader view"; return result; }

    ComPtr<IDXGISurface> ui_surface;
    result = render.ui_texture.As(&ui_surface);
    if (FAILED(result)) { state.graphics_stage = L"Query finished-UI DXGI surface"; return result; }
    const D2D1_BITMAP_PROPERTIES1 bitmap_properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f * scale, 96.0f * scale);
    result = render.d2d_context->CreateBitmapFromDxgiSurface(
        ui_surface.Get(), &bitmap_properties, render.ui_target.GetAddressOf());
    if (FAILED(result)) { state.graphics_stage = L"Bind Direct2D finished-UI target"; return result; }

    if (render.direct_composition) {
        result = DCompositionCreateDevice(dxgi_device.Get(), __uuidof(IDCompositionDevice),
                                          reinterpret_cast<void**>(render.composition.GetAddressOf()));
        if (FAILED(result)) { state.graphics_stage = L"Create DirectComposition device"; return result; }
        result = render.composition->CreateTargetForHwnd(state.window, TRUE,
                                                          render.composition_target.GetAddressOf());
        if (FAILED(result)) { state.graphics_stage = L"Create DirectComposition window target"; return result; }
        result = render.composition->CreateVisual(render.composition_visual.GetAddressOf());
        if (FAILED(result)) { state.graphics_stage = L"Create DirectComposition visual"; return result; }
        result = render.composition_visual->SetContent(render.swap_chain.Get());
        if (FAILED(result)) { state.graphics_stage = L"Attach swap chain to visual"; return result; }
        result = render.composition_target->SetRoot(render.composition_visual.Get());
        if (FAILED(result)) { state.graphics_stage = L"Attach visual to window"; return result; }
        result = render.composition->Commit();
        if (FAILED(result)) state.graphics_stage = L"Commit DirectComposition tree";
    }
    return result;
}

void set_brush(D2D1_COLOR_F value) {
    state.render.brush->SetColor(value);
}

void fill_round(float left, float top, float right, float bottom, float radius,
                D2D1_COLOR_F value) {
    set_brush(value);
    state.render.d2d_context->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(left, top, right, bottom), radius, radius),
        state.render.brush.Get());
}

void stroke_round(float left, float top, float right, float bottom, float radius,
                  float width, D2D1_COLOR_F value) {
    set_brush(value);
    state.render.d2d_context->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(left, top, right, bottom), radius, radius),
        state.render.brush.Get(), width);
}

void draw_text(const std::wstring& text, IDWriteTextFormat* format,
               float left, float top, float right, float bottom, D2D1_COLOR_F value) {
    set_brush(value);
    state.render.d2d_context->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()),
                                        format, D2D1::RectF(left, top, right, bottom),
                                        state.render.brush.Get(),
                                        D2D1_DRAW_TEXT_OPTIONS_CLIP,
                                        DWRITE_MEASURING_MODE_NATURAL);
}

ComPtr<ID2D1Bitmap1> icon_for(const AppEntry& entry) {
    const std::wstring key = entry.link.wstring();
    const auto found = state.render.icon_cache.find(key);
    if (found != state.render.icon_cache.end()) return found->second;
    SHFILEINFOW information{};
    if (!SHGetFileInfoW(key.c_str(), 0, &information, sizeof(information),
                        SHGFI_ICON | SHGFI_LARGEICON) || !information.hIcon) return {};
    ComPtr<IWICBitmap> wic_bitmap;
    if (FAILED(state.render.wic_factory->CreateBitmapFromHICON(
            information.hIcon, wic_bitmap.GetAddressOf()))) {
        DestroyIcon(information.hIcon);
        return {};
    }
    DestroyIcon(information.hIcon);
    ComPtr<ID2D1Bitmap1> bitmap;
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(state.render.d2d_context->CreateBitmapFromWicBitmap(
            wic_bitmap.Get(), &properties, bitmap.GetAddressOf()))) return {};
    state.render.icon_cache.emplace(key, bitmap);
    return bitmap;
}

void draw_search() {
    auto& render = state.render;
    fill_round(kSearchX, kSearchY, kSearchX + kSearchWidth,
               kSearchY + kSearchHeight, 12.0f, color(0.0f, 0.075f, 0.043f, 0.975f));
    stroke_round(kSearchX, kSearchY, kSearchX + kSearchWidth,
                 kSearchY + kSearchHeight, 12.0f, 2.0f,
                 color(0.31f, 0.68f, 0.47f, 0.86f));
    set_brush(color(0.46f, 0.82f, 0.57f, 0.96f));
    render.d2d_context->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(40, 44), 9, 9),
                                    render.brush.Get(), 2.0f);
    render.d2d_context->DrawLine(D2D1::Point2F(46.5f, 50.5f),
                                 D2D1::Point2F(53.5f, 57.5f), render.brush.Get(), 2.0f);
    draw_text(L"KALWER", render.tiny_format.Get(), 67, 17, 260, 30,
              color(0.30f, 0.68f, 0.47f));
    draw_text(L"> PTY   < JOBS   ? GOOGLE   ↑↓ SCROLL   ↵ GO",
              render.tiny_format.Get(), 350, 58, 625, 72,
              color(0.46f, 0.67f, 0.52f));

    const std::wstring query = window_text(state.edit);
    const std::wstring display = query.empty() ? L"SEARCH THE VAULT" : query;
    const D2D1_COLOR_F input_color = query.empty()
        ? color(0.46f, 0.67f, 0.52f) : color(0.81f, 0.89f, 0.82f);
    ComPtr<IDWriteTextLayout> layout;
    render.write_factory->CreateTextLayout(display.c_str(), static_cast<UINT32>(display.size()),
                                            render.input_format.Get(), 548.0f, 31.0f,
                                            layout.GetAddressOf());
    if (!layout) return;
    constexpr float text_x = 67.0f;
    constexpr float text_y = 31.0f;
    if (!query.empty()) {
        DWORD start = 0, end = 0;
        SendMessageW(state.edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                     reinterpret_cast<LPARAM>(&end));
        if (end > start) {
            UINT32 count = 0;
            layout->HitTestTextRange(start, end - start, text_x, text_y, nullptr, 0, &count);
            std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
            if (count) {
                layout->HitTestTextRange(start, end - start, text_x, text_y,
                                         metrics.data(), count, &count);
                set_brush(color(0.16f, 0.48f, 0.29f, 0.92f));
                for (const auto& metric : metrics) {
                    render.d2d_context->FillRectangle(
                        D2D1::RectF(metric.left, metric.top, metric.left + metric.width,
                                    metric.top + metric.height), render.brush.Get());
                }
            }
        }
    }
    set_brush(input_color);
    render.d2d_context->DrawTextLayout(D2D1::Point2F(text_x, text_y), layout.Get(),
                                        render.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (!query.empty()) {
        DWORD caret = 0;
        SendMessageW(state.edit, EM_GETSEL, reinterpret_cast<WPARAM>(&caret), 0);
        float caret_x = 0.0f, caret_y = 0.0f;
        DWRITE_HIT_TEST_METRICS metrics{};
        layout->HitTestTextPosition(caret, FALSE, &caret_x, &caret_y, &metrics);
        set_brush(color(0.62f, 0.91f, 0.70f, 0.92f));
        render.d2d_context->FillRectangle(
            D2D1::RectF(text_x + caret_x, text_y + caret_y,
                        text_x + caret_x + 1.8f, text_y + caret_y + metrics.height),
            render.brush.Get());
    }
}

void draw_results() {
    auto& render = state.render;
    fill_round(kSearchX, kResultsY - 6, kSearchX + kSearchWidth,
               kLogicalHeight - 4.0f, 12.0f, color(0.0f, 0.075f, 0.043f, 0.88f));
    const int visible_end = std::min(static_cast<int>(state.results.size()),
                                     state.scroll_offset + kVisibleResults);
    for (int index = state.scroll_offset; index < visible_end; ++index) {
        const AppEntry& result = state.results[static_cast<size_t>(index)];
        const int display_row = index - state.scroll_offset;
        const float y = kResultsY + display_row * kRowPitch;
        const bool selected = index == state.selection;
        fill_round(kResultX, y, kResultX + kResultWidth, y + kRowHeight, 10.0f,
                   selected ? color(0.02f, 0.20f, 0.105f, 0.97f)
                            : color(0.0f, 0.105f, 0.057f, 0.90f));
        stroke_round(kResultX, y, kResultX + kResultWidth, y + kRowHeight, 10.0f,
                     selected ? 2.0f : 1.0f,
                     color(0.31f, 0.68f, 0.47f, selected ? 0.90f : 0.20f));

        if (result.link.wstring().rfind(L"::", 0) != 0) {
            if (ComPtr<ID2D1Bitmap1> icon = icon_for(result)) {
                render.d2d_context->DrawBitmap(icon.Get(),
                    D2D1::RectF(kResultX + 12, y + 10, kResultX + 50, y + 48),
                    0.94f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            }
        } else {
            fill_round(kResultX + 12, y + 10, kResultX + 50, y + 48, 7.0f,
                       color(0.30f, 0.68f, 0.47f, 0.90f));
            const wchar_t* glyph = result.link == L"::calculator" ? L"="
                                 : result.link == L"::google" ? L"?" : L">";
            draw_text(glyph, render.title_format.Get(), kResultX + 24, y + 17,
                      kResultX + 48, y + 43, color(0.0f, 0.075f, 0.043f));
        }
        draw_text(result.title, render.title_format.Get(), kResultX + 62, y + 9,
                  kResultX + 530, y + 31, color(0.81f, 0.89f, 0.82f));
        draw_text(result.subtitle, render.subtitle_format.Get(), kResultX + 62, y + 32,
                  kResultX + 530, y + 52, color(0.46f, 0.67f, 0.52f));
        if (result.pinned) {
            draw_text(L"★", render.subtitle_format.Get(), kResultX + 548, y + 20,
                      kResultX + 567, y + 40, color(0.62f, 0.91f, 0.70f));
        }
        std::wostringstream rank;
        rank << std::setw(2) << std::setfill(L'0') << index + 1;
        draw_text(rank.str(), render.subtitle_format.Get(), kResultX + 570, y + 20,
                  kResultX + 597, y + 42, color(0.30f, 0.68f, 0.47f));
    }
    if (state.results.empty()) {
        draw_text(L"NO RESULTS", render.title_format.Get(), 42, kResultsY + 16,
                  400, kResultsY + 44, color(0.30f, 0.68f, 0.47f));
    }
}

HRESULT render_frame() {
    auto& render = state.render;
    if (!render.swap_chain || !render.ui_target) return E_FAIL;
    const auto now = std::chrono::steady_clock::now();
    float opening = 1.0f;
    if (state.opening) {
        const float elapsed = std::chrono::duration<float>(now - state.opened_at).count();
        opening = std::clamp(elapsed / 0.72f, 0.0f, 1.0f);
        if (opening >= 1.0f) state.opening = false;
    }
    const float target = static_cast<float>(state.selection - state.scroll_offset);
    state.selection_visual += (target - state.selection_visual) * 0.24f;

    render.d2d_context->SetTarget(render.ui_target.Get());
    render.d2d_context->SetTransform(D2D1::Matrix3x2F::Identity());
    render.d2d_context->BeginDraw();
    render.d2d_context->Clear(color(0, 0, 0, 0));
    draw_search();
    draw_results();
    HRESULT result = render.d2d_context->EndDraw();
    render.d2d_context->SetTarget(nullptr);
    if (FAILED(result)) return result;

    render.d3d_context->OMSetRenderTargets(1, render.back_view.GetAddressOf(), nullptr);
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(render.pixel_width);
    viewport.Height = static_cast<float>(render.pixel_height);
    viewport.MaxDepth = 1.0f;
    render.d3d_context->RSSetViewports(1, &viewport);
    render.d3d_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    render.d3d_context->VSSetShader(render.vertex_shader.Get(), nullptr, 0);
    render.d3d_context->PSSetShader(render.pixel_shader.Get(), nullptr, 0);
    ID3D11ShaderResourceView* view = render.ui_view.Get();
    render.d3d_context->PSSetShaderResources(0, 1, &view);
    ID3D11SamplerState* sampler = render.sampler.Get();
    render.d3d_context->PSSetSamplers(0, 1, &sampler);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = render.d3d_context->Map(render.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD,
                                     0, &mapped);
    if (FAILED(result)) return result;
    struct alignas(16) ShaderParameters {
        float logical_width;
        float logical_height;
        float opening;
        float selection_y;
        std::int32_t has_results;
        float padding[3];
    } parameters{
        static_cast<float>(kLogicalWidth),
        static_cast<float>(kLogicalHeight),
        opening,
        kResultsY + state.selection_visual * kRowPitch,
        state.results.empty() ? 0 : 1,
        {0.0f, 0.0f, 0.0f},
    };
    std::memcpy(mapped.pData, &parameters, sizeof(parameters));
    render.d3d_context->Unmap(render.constants.Get(), 0);
    ID3D11Buffer* constants = render.constants.Get();
    render.d3d_context->PSSetConstantBuffers(0, 1, &constants);
    render.d3d_context->Draw(3, 0);
    ID3D11ShaderResourceView* null_view = nullptr;
    render.d3d_context->PSSetShaderResources(0, 1, &null_view);
    result = render.swap_chain->Present(1, 0);
    state.render_dirty = state.opening || std::abs(target - state.selection_visual) > 0.01f;
    return result;
}

float monitor_scale(HMONITOR monitor) {
    UINT dpi_x = 96, dpi_y = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);
    return std::clamp(dpi_x / 96.0f, 1.0f, 3.0f);
}

HRESULT position_and_resize() {
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO information{};
    information.cbSize = sizeof(information);
    GetMonitorInfoW(monitor, &information);
    const float scale = monitor_scale(monitor);
    const int width = static_cast<int>(std::lround(kLogicalWidth * scale));
    const int height = static_cast<int>(std::lround(kLogicalHeight * scale));
    const int x = information.rcWork.left +
                  ((information.rcWork.right - information.rcWork.left) - width) / 2;
    const int y = information.rcWork.top +
                  ((information.rcWork.bottom - information.rcWork.top) - height) / 3;
    SetWindowPos(state.window, HWND_TOPMOST, x, y, width, height,
                 SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    if (state.render.pixel_width != static_cast<UINT>(width) ||
        state.render.pixel_height != static_cast<UINT>(height)) {
        return create_size_resources(static_cast<UINT>(width), static_cast<UINT>(height), scale);
    }
    return S_OK;
}

void show_launcher() {
    const auto now = std::chrono::steady_clock::now();
    const bool restore = !state.restored_query.empty() &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - state.hidden_at).count() <= 3000;
    if (!restore) {
        state.restored_query.clear();
        set_window_text_preserving_end(state.edit, L"");
        update_results();
    } else {
        set_window_text_preserving_end(state.edit, state.restored_query);
        update_results();
        state.selection = std::clamp(state.restored_selection, 0,
                                     std::max(0, static_cast<int>(state.results.size()) - 1));
        state.scroll_offset = std::clamp(state.restored_scroll, 0,
            std::max(0, static_cast<int>(state.results.size()) - kVisibleResults));
    }
    const HRESULT resize_result = position_and_resize();
    if (FAILED(resize_result)) {
        const std::wstring message = L"Could not create the transparent GPU surface.\nStage: " +
                                     state.graphics_stage;
        fail_message(message.c_str(), resize_result);
        return;
    }
    state.visible = true;
    state.opening = true;
    state.opened_at = now;
    state.render_dirty = true;
    ShowWindow(state.window, SW_SHOWNORMAL);
    SetForegroundWindow(state.window);
    SetFocus(state.edit);
    render_frame();
}

void hide_launcher() {
    if (!state.visible) return;
    state.restored_query = window_text(state.edit);
    state.restored_selection = state.selection;
    state.restored_scroll = state.scroll_offset;
    state.hidden_at = std::chrono::steady_clock::now();
    ShowWindow(state.window, SW_HIDE);
    state.visible = false;
}

LRESULT CALLBACK edit_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_KEYDOWN) {
        switch (wparam) {
            case VK_UP: move_selection(-1); return 0;
            case VK_DOWN: move_selection(1); return 0;
            case VK_PRIOR: move_selection(-kSelectableResults); return 0;
            case VK_NEXT: move_selection(kSelectableResults); return 0;
            case VK_ESCAPE: hide_launcher(); return 0;
            case VK_RETURN:
                if (GetKeyState(VK_SHIFT) & 0x8000) toggle_favorite();
                else activate_selection();
                return 0;
            case VK_TAB: return 0;
        }
    }
    return CallWindowProcW(state.edit_proc, window, message, wparam, lparam);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            state.edit = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                -1000, -1000, 1, 1, window, reinterpret_cast<HMENU>(100),
                state.instance, nullptr);
            state.edit_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                state.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(edit_window_proc)));
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == 100 && HIWORD(wparam) == EN_CHANGE) {
                update_results();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_HOTKEY:
            if (wparam == kHotkeyId) {
                if (state.visible) hide_launcher();
                else show_launcher();
            }
            return 0;
        case kToggleMessage:
            if (state.visible) hide_launcher();
            else show_launcher();
            return 0;
        case WM_TIMER:
            if (state.visible && (state.render_dirty || state.opening)) render_frame();
            return 0;
        case WM_MOUSEWHEEL:
            move_selection(GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -1 : 1);
            return 0;
        case WM_MOUSEMOVE: {
            if (!state.visible) break;
            const float scale = state.render.scale;
            const float logical_y = GET_Y_LPARAM(lparam) / scale;
            const int row = static_cast<int>((logical_y - kResultsY) / kRowPitch);
            if (logical_y >= kResultsY && row >= 0 && row < kSelectableResults) {
                const int index = state.scroll_offset + row;
                if (index < static_cast<int>(state.results.size()) && index != state.selection) {
                    state.selection = index;
                    state.render_dirty = true;
                }
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const float scale = state.render.scale;
            const float logical_y = GET_Y_LPARAM(lparam) / scale;
            const int row = static_cast<int>((logical_y - kResultsY) / kRowPitch);
            if (logical_y >= kResultsY && row >= 0 && row < kSelectableResults) {
                const int index = state.scroll_offset + row;
                if (index < static_cast<int>(state.results.size())) {
                    state.selection = index;
                    activate_selection();
                }
            }
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wparam) == WA_INACTIVE && state.visible) hide_launcher();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            EndPaint(window, &paint);
            if (state.visible) render_frame();
            return 0;
        }
        case WM_DESTROY:
            if (state.hotkey_registered) UnregisterHotKey(window, kHotkeyId);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    state.instance = instance;
    state.mutex = CreateMutexW(nullptr, FALSE, L"Local\\KalwerWindowsResident-v1");
    if (state.mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kWindowClass, nullptr)) {
            PostMessageW(existing, kToggleMessage, 0, 0);
        }
        CloseHandle(state.mutex);
        CoUninitialize();
        return 0;
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&window_class)) return 1;

    state.window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP,
        kWindowClass, kWindowTitle, WS_POPUP, 0, 0, kLogicalWidth, kLogicalHeight,
        nullptr, nullptr, instance, nullptr);
    if (!state.window) return 1;
    const int corner_preference = 1;
    DwmSetWindowAttribute(state.window, 33, &corner_preference, sizeof(corner_preference));

    HRESULT result = create_device_independent_resources();
    if (SUCCEEDED(result)) result = create_graphics_device();
    if (FAILED(result)) {
        fail_message(L"Could not initialize the Direct3D 11 renderer.", result);
        DestroyWindow(state.window);
        CoUninitialize();
        return 1;
    }
    load_favorites();
    load_apps();
    update_results();
    state.hotkey_registered = RegisterHotKey(state.window, kHotkeyId,
                                              MOD_ALT | MOD_NOREPEAT, VK_SPACE);
    if (!state.hotkey_registered) {
        fail_message(L"Alt+Space is already owned by another application.\n"
                     L"Kalwer can still be toggled by launching it again.");
    }
    SetTimer(state.window, kTimerId, 16, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (state.mutex) CloseHandle(state.mutex);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
