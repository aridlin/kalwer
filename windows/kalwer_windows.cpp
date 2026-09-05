#include "../file_index.hpp"
#include "../launcher_commands.hpp"
#include "../update_status.hpp"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <bcrypt.h>
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <knownfolders.h>
#include <propkey.h>
#include <shellscalingapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <winhttp.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kWindowClass[] = L"KalwerWindowsHost";
constexpr wchar_t kWindowTitle[] = L"Kalwer";
constexpr int kLogicalWidth = 650;
constexpr int kLogicalHeight = 632;
constexpr int kPopupLogicalWidth = 510;
constexpr int kExpandedLogicalWidth = kLogicalWidth + kPopupLogicalWidth;
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
constexpr UINT kCommandChangedMessage = WM_APP + 42;
constexpr float kCloseDurationMs = 280.0f;
constexpr wchar_t kKalwerVersion[] = L"0.4.0";
constexpr wchar_t kLatestReleaseUrl[] =
    L"https://github.com/aridlin/kalwer/releases/latest";

enum class PopupButton {
    none,
    copy,
    background,
    close,
};

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

bool running_under_wine() {
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    return module && GetProcAddress(module, "wine_get_version");
}

std::filesystem::path executable_path() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

std::vector<int> version_parts(const std::wstring& version) {
    std::vector<int> parts;
    std::size_t cursor = 0;
    while (cursor < version.size()) {
        while (cursor < version.size() && !std::iswdigit(version[cursor])) ++cursor;
        if (cursor >= version.size()) break;
        wchar_t* end = nullptr;
        const long value = std::wcstol(version.c_str() + cursor, &end, 10);
        if (!end || end == version.c_str() + cursor) break;
        parts.push_back(static_cast<int>(std::min<long>(value, 1000000)));
        cursor = static_cast<std::size_t>(end - version.c_str());
        if (cursor < version.size() && version[cursor] != L'.') break;
        ++cursor;
    }
    return parts;
}

bool version_is_newer(const std::wstring& candidate, const std::wstring& current) {
    std::vector<int> left = version_parts(candidate);
    std::vector<int> right = version_parts(current);
    const std::size_t count = std::max(left.size(), right.size());
    left.resize(count);
    right.resize(count);
    return left > right;
}

bool http_get(const std::wstring& url, std::vector<std::uint8_t>* body,
              std::wstring* effective_url = nullptr) {
    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    wchar_t host[512]{};
    wchar_t path[4096]{};
    wchar_t extra[4096]{};
    components.lpszHostName = host;
    components.dwHostNameLength = static_cast<DWORD>(std::size(host));
    components.lpszUrlPath = path;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    components.lpszExtraInfo = extra;
    components.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        return false;
    }
    const std::wstring request_path =
        std::wstring(path, components.dwUrlPathLength) +
        std::wstring(extra, components.dwExtraInfoLength);
    HINTERNET session = WinHttpOpen(
        L"Kalwer/0.3 automatic updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 4000, 4000, 4000, 30000);
    HINTERNET connection = WinHttpConnect(
        session, std::wstring(host, components.dwHostNameLength).c_str(),
        components.nPort, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return false;
    }
    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS
        ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connection, L"GET", request_path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    bool okay = request &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (okay) {
        okay = WinHttpQueryHeaders(
            request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
            WINHTTP_NO_HEADER_INDEX) && status >= 200 && status < 300;
    }
    if (okay && effective_url) {
        DWORD bytes = 0;
        WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &bytes);
        std::vector<wchar_t> resolved(bytes / sizeof(wchar_t) + 1);
        if (bytes && WinHttpQueryOption(request, WINHTTP_OPTION_URL,
                                        resolved.data(), &bytes)) {
            *effective_url = resolved.data();
        }
    }
    if (okay && body) {
        body->clear();
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) {
                okay = false;
                break;
            }
            if (!available) break;
            const std::size_t offset = body->size();
            if (offset + available > 64u * 1024u * 1024u) {
                okay = false;
                break;
            }
            body->resize(offset + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, body->data() + offset, available, &read)) {
                okay = false;
                break;
            }
            body->resize(offset + read);
        }
    }
    if (request) WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return okay;
}

std::string sha256_hex(const std::vector<std::uint8_t>& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD hash_size = 0;
    DWORD received = 0;
    std::string result;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) != 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                          &received, 0) != 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&hash_size), sizeof(hash_size),
                          &received, 0) != 0) {
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        return result;
    }
    std::vector<UCHAR> object(object_size);
    std::vector<UCHAR> digest(hash_size);
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                         nullptr, 0, 0) == 0 &&
        BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()),
                       static_cast<ULONG>(bytes.size()), 0) == 0 &&
        BCryptFinishHash(hash, digest.data(), hash_size, 0) == 0) {
        static constexpr char digits[] = "0123456789abcdef";
        result.reserve(digest.size() * 2);
        for (UCHAR byte : digest) {
            result.push_back(digits[byte >> 4]);
            result.push_back(digits[byte & 15]);
        }
    }
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

kalwer::UpdateStatus update_status;
std::atomic<HWND> update_window{nullptr};
bool updated_on_launch = false;
constexpr UINT kUpdateNoticeMessage = WM_APP + 44;
void announce_update(const std::string& message) {
    update_status.set(message);
    auto* body = new std::wstring(wide(message));
    if (!PostMessageW(update_window.load(), kUpdateNoticeMessage, 0, reinterpret_cast<LPARAM>(body))) delete body;
}

void check_for_update() {
    if (running_under_wine()) { update_status.set("Automatic updates are disabled under Wine."); return; }
    const std::filesystem::path target = executable_path();
    if (target.empty()) return;
    std::filesystem::path pending = target;
    pending += L".update.exe";
    std::error_code error;
    if (std::filesystem::exists(pending, error)) { announce_update("An update is ready. Restart Kalwer to choose when to install it."); return; }
    std::filesystem::path partial = pending;
    partial += L".partial";
    {
        std::ofstream probe(partial, std::ios::binary | std::ios::trunc);
        if (!probe) { update_status.set("This installation is not writable. Download the new version from GitHub to update it manually."); return; }
    }
    std::filesystem::remove(partial, error);

    std::wstring effective;
    if (!http_get(kLatestReleaseUrl, nullptr, &effective)) { announce_update("Could not check GitHub for updates. Your current version is unchanged."); return; }
    const std::wstring marker = L"/tag/v";
    const std::size_t marker_at = effective.rfind(marker);
    if (marker_at == std::wstring::npos) return;
    const std::wstring version = effective.substr(marker_at + marker.size());
    if (!version_is_newer(version, kKalwerVersion)) { update_status.set("Kalwer v" + utf8(kKalwerVersion) + " is up to date."); return; }

    announce_update("Kalwer v" + utf8(version) + " is available. Downloading and verifying the update…");
    kalwer::UpdateAttempt attempt(announce_update);
    const std::wstring asset =
        L"https://github.com/aridlin/kalwer/releases/download/v" + version +
        L"/kalwer.exe";
    std::vector<std::uint8_t> checksum_bytes;
    if (!http_get(asset + L".sha256", &checksum_bytes)) return;
    std::string expected_checksum(checksum_bytes.begin(), checksum_bytes.end());
    const std::size_t separator = expected_checksum.find_first_of(" \t\r\n");
    expected_checksum = expected_checksum.substr(0, separator);
    std::transform(expected_checksum.begin(), expected_checksum.end(),
                   expected_checksum.begin(), [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (expected_checksum.size() != 64 ||
        !std::all_of(expected_checksum.begin(), expected_checksum.end(), [](char character) {
            return std::isxdigit(static_cast<unsigned char>(character));
        })) return;
    std::vector<std::uint8_t> bytes;
    if (!http_get(asset, &bytes) || bytes.size() < 65536 ||
        bytes[0] != 'M' || bytes[1] != 'Z') return;
    const std::uint32_t pe_offset =
        static_cast<std::uint32_t>(bytes[0x3c]) |
        (static_cast<std::uint32_t>(bytes[0x3d]) << 8) |
        (static_cast<std::uint32_t>(bytes[0x3e]) << 16) |
        (static_cast<std::uint32_t>(bytes[0x3f]) << 24);
    if (static_cast<std::size_t>(pe_offset) + 6 >= bytes.size() || bytes[pe_offset] != 'P' ||
        bytes[pe_offset + 1] != 'E' || bytes[pe_offset + 2] != 0 ||
        bytes[pe_offset + 3] != 0 || bytes[pe_offset + 4] != 0x64 ||
        bytes[pe_offset + 5] != 0x86) return;
    if (sha256_hex(bytes) != expected_checksum) return;
    {
        std::ofstream output(partial, std::ios::binary | std::ios::trunc);
        if (!output) return;
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            output.close();
            std::filesystem::remove(partial, error);
            return;
        }
    }
    if (!MoveFileExW(partial.c_str(), pending.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(partial, error);
    } else {
        attempt.complete("Kalwer v" + utf8(version) + " is downloaded and verified. Restart Kalwer to install it; you can choose Later at startup.");
    }
}

std::wstring quote_argument(const std::wstring& value) {
    std::wstring output = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
        } else {
            if (character == L'\"') output.append(backslashes * 2 + 1, L'\\');
            else output.append(backslashes, L'\\');
            backslashes = 0;
            output.push_back(character);
        }
    }
    output.append(backslashes * 2, L'\\');
    output.push_back(L'\"');
    return output;
}

bool launch_process(std::wstring command) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const bool launched = CreateProcessW(
        nullptr, command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &startup, &process) != FALSE;
    if (launched) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    return launched;
}

bool handle_update_bootstrap() {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    const std::filesystem::path module = executable_path();
    if (!arguments || module.empty()) {
        if (arguments) LocalFree(arguments);
        return false;
    }
    if (count >= 4 && std::wcscmp(arguments[1], L"--apply-update") == 0) {
        const DWORD parent_id = static_cast<DWORD>(std::wcstoul(arguments[2], nullptr, 10));
        const std::filesystem::path target = arguments[3];
        if (HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_id)) {
            WaitForSingleObject(parent, 30000);
            CloseHandle(parent);
        }
        const bool copied = CopyFileW(module.c_str(), target.c_str(), FALSE) != FALSE;
        if (!copied) MessageBoxW(nullptr, L"Kalwer could not replace the executable. Your previous version will be opened.", L"Kalwer update failed", MB_OK | MB_ICONERROR);
        if (!launch_process(quote_argument(target.wstring()) + (copied ? L" --updated" : L" --update-failed")))
            MessageBoxW(nullptr, L"Please start Kalwer manually. The updater could not restart it.", L"Kalwer update", MB_OK | MB_ICONERROR);
        LocalFree(arguments);
        return true;
    }

    std::filesystem::path pending = module;
    pending += L".update.exe";
    if (count >= 2 && std::wcscmp(arguments[1], L"--update-failed") == 0) { LocalFree(arguments); return false; }
    if (count >= 2 && std::wcscmp(arguments[1], L"--updated") == 0) {
        updated_on_launch = true;
        std::thread([pending] {
            for (int attempt = 0; attempt < 40; ++attempt) {
                if (DeleteFileW(pending.c_str())) return;
                Sleep(100);
            }
            MoveFileExW(pending.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }).detach();
        LocalFree(arguments);
        return false;
    }
    LocalFree(arguments);
    std::error_code error;
    if (!std::filesystem::exists(pending, error)) return false;
    if (MessageBoxW(nullptr, L"A downloaded Kalwer update is ready. Install it now?\n\nChoose Cancel to keep using this version and install later.",
                    L"Kalwer update ready", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) return false;
    const std::wstring command = quote_argument(pending.wstring()) +
        L" --apply-update " + std::to_wstring(GetCurrentProcessId()) + L" " +
        quote_argument(module.wstring());
    return launch_process(command);
}

struct AppEntry {
    std::wstring title;
    std::wstring subtitle;
    std::filesystem::path link;
    std::wstring folded;
    std::vector<int> matches;
    std::wstring payload;
    std::wstring app_user_model_id;
    int score = 0;
    bool pinned = false;
};

struct CommandJob {
    std::uint64_t id = 0;
    std::wstring command;
    HPCON pseudo_console = nullptr;
    HANDLE input_write = nullptr;
    HANDLE output_read = nullptr;
    HANDLE process = nullptr;
    HANDLE process_thread = nullptr;
    std::thread reader;
    std::thread waiter;
    std::mutex output_mutex;
    std::string output;
    std::atomic<bool> running{true};
    std::atomic<DWORD> exit_code{STILL_ACTIVE};
    bool background = false;
    bool interacted = false;
    bool escape = false;
    bool csi = false;
    bool osc = false;
    bool carriage_return = false;
    std::chrono::steady_clock::time_point finished_at{};
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
    ComPtr<IDWriteTextFormat> terminal_format;
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
    std::vector<std::unique_ptr<CommandJob>> jobs;
    CommandJob* popup_job = nullptr;
    std::optional<kalwer::PopupDocument> popup_document;
    size_t popup_document_scroll = 0;
    std::uint64_t next_job_id = 0;
    int selection = 0;
    int scroll_offset = 0;
    float selection_visual = 0.0f;
    float scroll_visual = 0.0f;
    bool visible = false;
    bool opening = false;
    bool closing = false;
    bool render_dirty = true;
    bool hotkey_registered = false;
    bool settings_mode = false;
    bool popup_open = false;
    bool notification_icon_added = false;
    std::wstring graphics_stage;
    std::uint32_t prompt_retention_ms = 3000;
    std::uint32_t popup_line_ms = 220;
    std::uint32_t popup_expand_ms = 390;
    std::uint32_t output_close_ms = 2000;
    std::chrono::steady_clock::time_point opened_at{};
    std::chrono::steady_clock::time_point last_render_at{};
    std::chrono::steady_clock::time_point popup_opened_at{};
    std::chrono::steady_clock::time_point hidden_at{};
    std::chrono::steady_clock::time_point closing_at{};
    std::chrono::steady_clock::time_point last_input_at{};
    std::wstring restored_query;
    int restored_selection = 0;
    int restored_scroll = 0;
    bool search_selecting = false;
    DWORD search_selection_anchor = 0;
    bool popup_selecting = false;
    bool suppress_popup_launch_char = false;
    UINT32 popup_selection_anchor = 0;
    UINT32 popup_selection_end = 0;
    PopupButton popup_hover = PopupButton::none;
    PopupButton popup_pressed = PopupButton::none;
    bool icons_deferred = false;
};

State state;
kalwer_files::Index file_index;
unsigned long long file_request = 0, file_revision = 0;
std::wstring file_status = L"Indexing files…";

void fail_message(const wchar_t* text, HRESULT code = S_OK) {
    std::wostringstream stream;
    stream << text;
    if (FAILED(code)) stream << L"\nHRESULT 0x" << std::hex << static_cast<unsigned long>(code);
    MessageBoxW(nullptr, stream.str().c_str(), L"Kalwer", MB_OK | MB_ICONERROR);
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

void enumerate_apps_folder(std::vector<AppEntry>& output) {
    ComPtr<IShellItem> folder;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr,
                                    IID_PPV_ARGS(folder.GetAddressOf())))) return;
    ComPtr<IEnumShellItems> items;
    if (FAILED(folder->BindToHandler(nullptr, BHID_EnumItems,
                                     IID_PPV_ARGS(items.GetAddressOf())))) return;
    for (;;) {
        ComPtr<IShellItem> item;
        ULONG fetched = 0;
        if (items->Next(1, item.GetAddressOf(), &fetched) != S_OK || !fetched) break;
        PWSTR name = nullptr;
        PWSTR parsing = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) || !name) continue;
        item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsing);
        if (!parsing) {
            CoTaskMemFree(name);
            continue;
        }
        AppEntry entry;
        entry.title = name;
        entry.subtitle = L"APPLICATION";
        entry.link = parsing;
        ComPtr<IShellItem2> item2;
        if (SUCCEEDED(item.As(&item2))) {
            PWSTR app_id = nullptr;
            if (SUCCEEDED(item2->GetString(PKEY_AppUserModel_ID, &app_id)) && app_id) {
                entry.app_user_model_id = app_id;
                CoTaskMemFree(app_id);
            }
        }
        entry.folded = lower_copy(entry.title + L" " + entry.subtitle + L" " +
                                  entry.app_user_model_id);
        output.push_back(std::move(entry));
        CoTaskMemFree(name);
        CoTaskMemFree(parsing);
    }
}

void enumerate_app_paths(HKEY hive, REGSAM view, std::vector<AppEntry>& output) {
    HKEY root = nullptr;
    if (RegOpenKeyExW(hive, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths",
                      0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | view, &root) !=
        ERROR_SUCCESS) return;
    for (DWORD index = 0;; ++index) {
        wchar_t name[512]{};
        DWORD name_length = static_cast<DWORD>(std::size(name));
        const LONG enumeration = RegEnumKeyExW(root, index, name, &name_length, nullptr,
                                                nullptr, nullptr, nullptr);
        if (enumeration == ERROR_NO_MORE_ITEMS) break;
        if (enumeration != ERROR_SUCCESS) continue;
        HKEY app_key = nullptr;
        if (RegOpenKeyExW(root, name, 0, KEY_QUERY_VALUE | view, &app_key) != ERROR_SUCCESS) {
            continue;
        }
        DWORD type = 0;
        DWORD bytes = 0;
        if (RegQueryValueExW(app_key, nullptr, nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= sizeof(wchar_t)) {
            std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 2);
            if (RegQueryValueExW(app_key, nullptr, nullptr, &type,
                                 reinterpret_cast<BYTE*>(value.data()), &bytes) ==
                ERROR_SUCCESS) {
                std::wstring target = value.data();
                if (type == REG_EXPAND_SZ) {
                    std::vector<wchar_t> expanded(32768);
                    const DWORD count = ExpandEnvironmentStringsW(
                        target.c_str(), expanded.data(), static_cast<DWORD>(expanded.size()));
                    if (count && count < expanded.size()) target.assign(expanded.data());
                }
                target = trim_copy(target);
                if (target.size() >= 2 && target.front() == L'"' && target.back() == L'"') {
                    target = target.substr(1, target.size() - 2);
                }
                if (!target.empty()) {
                    AppEntry entry;
                    entry.title.assign(name, name_length);
                    if (lower_copy(std::filesystem::path(entry.title).extension().wstring()) ==
                        L".exe") entry.title = std::filesystem::path(entry.title).stem().wstring();
                    entry.subtitle = L"INSTALLED APPLICATION";
                    entry.link = target;
                    entry.folded = lower_copy(entry.title + L" " + entry.subtitle + L" " + target);
                    output.push_back(std::move(entry));
                }
            }
        }
        RegCloseKey(app_key);
    }
    RegCloseKey(root);
}

void load_apps() {
    std::vector<AppEntry> apps;
    auto enumerate_known_folder = [&](REFKNOWNFOLDERID id) {
        PWSTR raw = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw))) {
            enumerate_programs_at(raw, apps);
            CoTaskMemFree(raw);
        }
    };
    enumerate_known_folder(FOLDERID_Programs);
    enumerate_known_folder(FOLDERID_CommonPrograms);
    // Wine's virtual shell/registry enumerators can block indefinitely. Real
    // Windows exposes these quickly; together they cover packaged apps and
    // classic applications that do not install Start-menu shortcuts.
    if (!running_under_wine()) {
        enumerate_known_folder(FOLDERID_Desktop);
        enumerate_known_folder(FOLDERID_PublicDesktop);
        enumerate_apps_folder(apps);
        enumerate_app_paths(HKEY_CURRENT_USER, 0, apps);
        enumerate_app_paths(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, apps);
        enumerate_app_paths(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, apps);
    }
    std::sort(apps.begin(), apps.end(), [](const AppEntry& left, const AppEntry& right) {
        const std::wstring left_title = lower_copy(left.title);
        const std::wstring right_title = lower_copy(right.title);
        if (left_title != right_title) return left_title < right_title;
        return lower_copy(left.link.wstring()) < lower_copy(right.link.wstring());
    });
    apps.erase(std::unique(apps.begin(), apps.end(), [](const AppEntry& left,
                                                        const AppEntry& right) {
        return lower_copy(left.link.wstring()) == lower_copy(right.link.wstring());
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

std::uint32_t bounded_unsigned(const std::string& value, std::uint32_t fallback,
                               std::uint32_t minimum, std::uint32_t maximum) {
    try {
        const unsigned long parsed = std::stoul(value);
        return std::clamp(static_cast<std::uint32_t>(parsed), minimum, maximum);
    } catch (...) {
        return fallback;
    }
}

void load_settings() {
    std::ifstream input(local_data_directory() / L"settings-v1.ini", std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        const size_t separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "prompt_retention_ms") {
            state.prompt_retention_ms = bounded_unsigned(value, 3000, 0, 30000);
        } else if (key == "popup_line_ms") {
            state.popup_line_ms = bounded_unsigned(value, 220, 50, 2000);
        } else if (key == "popup_expand_ms") {
            state.popup_expand_ms = bounded_unsigned(value, 390, 50, 2500);
        } else if (key == "output_close_ms") {
            state.output_close_ms = bounded_unsigned(value, 2000, 0, 30000);
        }
    }
}

void save_settings() {
    const auto directory = local_data_directory();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    std::ofstream output(directory / L"settings-v1.ini", std::ios::binary | std::ios::trunc);
    output << "prompt_retention_ms=" << state.prompt_retention_ms << '\n'
           << "popup_line_ms=" << state.popup_line_ms << '\n'
           << "popup_expand_ms=" << state.popup_expand_ms << '\n'
           << "output_close_ms=" << state.output_close_ms << '\n';
}

bool autostart_enabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD size = 0;
    const LONG result = RegQueryValueExW(
        key, L"Kalwer", nullptr, nullptr, nullptr, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool set_autostart(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LONG result = ERROR_SUCCESS;
    if (enabled) {
        std::vector<wchar_t> path(32768);
        const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                                static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) {
            RegCloseKey(key);
            return false;
        }
        const std::wstring command = L"\"" + std::wstring(path.data(), length) + L"\"";
        result = RegSetValueExW(key, L"Kalwer", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, L"Kalwer");
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
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

bool simple_integer_fraction(const std::wstring& source, long long& numerator,
                             long long& denominator) {
    const size_t slash = source.find(L'/');
    if (slash == std::wstring::npos || source.find(L'/', slash + 1) != std::wstring::npos) {
        return false;
    }
    const std::wstring left = trim_copy(source.substr(0, slash));
    const std::wstring right = trim_copy(source.substr(slash + 1));
    if (left.empty() || right.empty()) return false;
    wchar_t* left_end = nullptr;
    wchar_t* right_end = nullptr;
    numerator = std::wcstoll(left.c_str(), &left_end, 10);
    denominator = std::wcstoll(right.c_str(), &right_end, 10);
    return left_end && *left_end == L'\0' && right_end && *right_end == L'\0' &&
           denominator != 0;
}

bool approximate_fraction(double value, long long& numerator, long long& denominator) {
    constexpr long long maximum_denominator = 100000;
    const bool negative = value < 0.0;
    const double target = std::abs(value);
    double remaining = target;
    long long numerator_previous_two = 0;
    long long numerator_previous = 1;
    long long denominator_previous_two = 1;
    long long denominator_previous = 0;
    long long best_numerator = 0;
    long long best_denominator = 1;
    for (int iteration = 0; iteration < 32; ++iteration) {
        const double integral = std::floor(remaining);
        if (integral > static_cast<double>(std::numeric_limits<long long>::max() / 4)) break;
        const long long coefficient = static_cast<long long>(integral);
        if (numerator_previous != 0 && coefficient >
            (std::numeric_limits<long long>::max() - numerator_previous_two) /
                std::abs(numerator_previous)) break;
        if (denominator_previous != 0 && coefficient >
            (std::numeric_limits<long long>::max() - denominator_previous_two) /
                std::abs(denominator_previous)) break;
        const long long next_numerator = coefficient * numerator_previous +
                                         numerator_previous_two;
        const long long next_denominator = coefficient * denominator_previous +
                                           denominator_previous_two;
        if (next_denominator <= 0 || next_denominator > maximum_denominator) break;
        best_numerator = next_numerator;
        best_denominator = next_denominator;
        if (std::abs(static_cast<double>(best_numerator) / best_denominator - target) <
            1e-10) break;
        const double fractional = remaining - integral;
        if (fractional < 1e-14) break;
        remaining = 1.0 / fractional;
        numerator_previous_two = numerator_previous;
        numerator_previous = next_numerator;
        denominator_previous_two = denominator_previous;
        denominator_previous = next_denominator;
    }
    if (best_denominator <= 0) return false;
    numerator = negative ? -best_numerator : best_numerator;
    denominator = best_denominator;
    return true;
}

AppEntry calculator_entry(const std::wstring& value, const std::wstring& label) {
    AppEntry result;
    result.title = value;
    result.subtitle = label + L" · ENTER TO COPY";
    result.link = L"::calculator";
    result.payload = value;
    result.folded = lower_copy(result.title);
    return result;
}

std::vector<AppEntry> calculator_results(const std::wstring& query) {
    double value = 0.0;
    ExpressionParser parser(query);
    if (!parser.parse(value)) return {};
    std::vector<AppEntry> results;
    if (std::abs(value - std::round(value)) > 1e-11) {
        long long numerator = 0;
        long long denominator = 0;
        long long source_numerator = 0;
        long long source_denominator = 0;
        if (simple_integer_fraction(query, source_numerator, source_denominator)) {
            const long long divisor = std::gcd(source_numerator, source_denominator);
            numerator = source_numerator / divisor;
            denominator = source_denominator / divisor;
            if (denominator < 0) {
                denominator = -denominator;
                numerator = -numerator;
            }
        } else {
            approximate_fraction(value, numerator, denominator);
        }
        results.push_back(calculator_entry(format_number(value), L"DECIMAL"));
        if (denominator > 0) {
            results.push_back(calculator_entry(
                std::to_wstring(numerator) + L"/" + std::to_wstring(denominator),
                L"REDUCED FRACTION"));
            if (std::llabs(numerator) > denominator) {
                const long long whole = numerator / denominator;
                const long long remainder = std::llabs(numerator % denominator);
                if (remainder) {
                    results.push_back(calculator_entry(
                        std::to_wstring(whole) + L" " + std::to_wstring(remainder) +
                            L"/" + std::to_wstring(denominator),
                        L"MIXED NUMBER"));
                }
            }
        }
        results.push_back(calculator_entry(format_number(value * 100.0) + L"%",
                                           L"PERCENT"));
    } else {
        results.push_back(calculator_entry(format_number(value), L"CALCULATED RESULT"));
    }
    return results;
}

void update_results();
void add_background_results(std::vector<AppEntry>& output, const std::wstring& filter);

void adjust_setting(int direction) {
    if (!direction) return;
    auto adjust = [direction](std::uint32_t value, std::uint32_t step,
                              std::uint32_t minimum, std::uint32_t maximum) {
        const long long next = static_cast<long long>(value) +
                               static_cast<long long>(direction) * step;
        return static_cast<std::uint32_t>(std::clamp(
            next, static_cast<long long>(minimum), static_cast<long long>(maximum)));
    };
    switch (state.selection) {
        case 0:
            set_autostart(!autostart_enabled());
            break;
        case 1:
            state.prompt_retention_ms = adjust(
                state.prompt_retention_ms, 250, 0, 30000);
            break;
        case 2:
            state.popup_line_ms = adjust(state.popup_line_ms, 25, 50, 2000);
            break;
        case 3:
            state.popup_expand_ms = adjust(state.popup_expand_ms, 25, 50, 2500);
            break;
        case 4:
            state.output_close_ms = adjust(state.output_close_ms, 250, 0, 30000);
            break;
        default:
            return;
    }
    save_settings();
    state.render_dirty = true;
}

void leave_settings() {
    state.settings_mode = false;
    update_results();
    SetFocus(state.edit);
}

void update_results() {
    const std::wstring raw_query = window_text(state.edit);
    const std::wstring query = lower_copy(trim_copy(raw_query));
    std::vector<AppEntry> filtered;

    if (!raw_query.empty() && raw_query.front() == L'/') {
        for (const auto* c : kalwer::matching_commands(utf8(raw_query))) {
            AppEntry r; r.title = wide(std::string(c->name)); r.subtitle = wide(std::string(c->description));
            r.link = L"::slash"; r.payload = r.title; filtered.push_back(std::move(r));
        }
    } else if (!raw_query.empty() && raw_query.front() == L':') {
        file_request = file_index.request(utf8(raw_query.substr(1)));
        file_status = L"Searching indexed files…";
    } else if (!query.empty() && query.front() == L'<') {
        add_background_results(filtered, raw_query.substr(1));
    } else if (std::vector<AppEntry> calculations = calculator_results(query);
               !calculations.empty()) {
        filtered = std::move(calculations);
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
        command.payload = command.subtitle;
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
            if (left.pinned) {
                const std::wstring left_title = lower_copy(left.title);
                const std::wstring right_title = lower_copy(right.title);
                if (left_title != right_title) return left_title < right_title;
                return lower_copy(left.link.wstring()) < lower_copy(right.link.wstring());
            }
            if (left.score != right.score) return left.score > right.score;
            return lower_copy(left.title) < lower_copy(right.title);
        });
    }
    state.results = std::move(filtered);
    state.selection = 0;
    state.scroll_offset = 0;
    state.selection_visual = 0.0f;
    state.scroll_visual = 0.0f;
    state.render_dirty = true;
}

void poll_files() {
    if (!state.edit || state.settings_mode || window_text(state.edit).substr(0, 1) != L":") return;
    const auto reply = file_index.poll(file_revision);
    if (reply.request != file_request || reply.revision == file_revision) return;
    file_revision = reply.revision;
    file_status = wide(reply.status);
    state.results.clear();
    for (const auto& e : reply.entries) {
        AppEntry r;
        r.title = wide(e.name); r.subtitle = wide(e.path);
        r.link = kalwer_files::from_utf8(e.path);
        state.results.push_back(std::move(r));
    }
    state.selection = state.scroll_offset = 0;
    state.selection_visual = state.scroll_visual = 0;
    state.render_dirty = true;
}

std::wstring suggested_completion() {
    const auto input = window_text(state.edit);
    if (input.empty()) return {};
    if (input.front() == L'/') {
        auto commands = kalwer::matching_commands(utf8(input));
        if (!commands.empty()) return wide(std::string(commands[std::clamp(state.selection, 0, static_cast<int>(commands.size()) - 1)]->name));
    }
    if (state.selection >= 0 && state.selection < static_cast<int>(state.results.size())) {
        const auto& r = state.results[state.selection];
        const auto candidate = input.front() == L':' ? L":" + (input.find_first_of(L"/\\") != std::wstring::npos ? r.link.wstring() : r.title) : r.title;
        if (lower_copy(candidate).starts_with(lower_copy(input))) return candidate;
    }
    return {};
}

void move_selection(int delta) {
    if (state.settings_mode) {
        state.selection = std::clamp(state.selection + delta, 0, 4);
        state.scroll_offset = 0;
        state.render_dirty = true;
        return;
    }
    if (state.results.empty()) return;
    state.selection = std::clamp(state.selection + delta, 0,
                                 static_cast<int>(state.results.size()) - 1);
    if (state.selection < state.scroll_offset) state.scroll_offset = state.selection;
    if (state.selection >= state.scroll_offset + kSelectableResults) {
        state.scroll_offset = state.selection - kSelectableResults + 1;
    }
    const int maximum_offset = std::max(0, static_cast<int>(state.results.size()) -
                                           kSelectableResults);
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
HRESULT position_and_resize();

CommandJob* find_job(std::uint64_t id) {
    for (const auto& job : state.jobs) {
        if (job->id == id) return job.get();
    }
    return nullptr;
}

void append_job_output(CommandJob& job, const char* bytes, size_t length) {
    std::scoped_lock lock(job.output_mutex);
    for (size_t index = 0; index < length; ++index) {
        const unsigned char value = static_cast<unsigned char>(bytes[index]);
        if (job.carriage_return) {
            job.carriage_return = false;
            if (value == '\n') {
                job.output.push_back('\n');
                continue;
            }
            const size_t line = job.output.find_last_of('\n');
            job.output.resize(line == std::string::npos ? 0 : line + 1);
        }
        if (job.osc) {
            if (value == 7) job.osc = false;
            else if (value == 27) job.escape = true;
            else if (job.escape && value == '\\') {
                job.osc = false;
                job.escape = false;
            }
            continue;
        }
        if (job.csi) {
            if (value >= 0x40 && value <= 0x7e) job.csi = false;
            continue;
        }
        if (job.escape) {
            job.escape = false;
            if (value == '[') job.csi = true;
            else if (value == ']') job.osc = true;
            continue;
        }
        if (value == 27) {
            job.escape = true;
        } else if (value == '\r') {
            job.carriage_return = true;
        } else if (value == '\b') {
            const size_t line = job.output.find_last_of('\n');
            if (!job.output.empty() &&
                (line == std::string::npos || job.output.size() > line + 1)) {
                job.output.pop_back();
            }
        } else if (value == '\t') {
            job.output.append(4, ' ');
        } else if (value == '\n' || value >= 0x20) {
            job.output.push_back(static_cast<char>(value));
        }
    }
    constexpr size_t maximum_output = 512 * 1024;
    if (job.output.size() > maximum_output) {
        const size_t keep_from = job.output.size() - maximum_output;
        const size_t next_line = job.output.find('\n', keep_from);
        job.output.erase(0, next_line == std::string::npos ? keep_from : next_line + 1);
    }
}

void show_notification(const std::wstring& title, const std::wstring& body, bool failed = false) {
    NOTIFYICONDATAW notification{};
    notification.cbSize = sizeof(notification);
    notification.hWnd = state.window;
    notification.uID = 7;
    notification.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    notification.uCallbackMessage = WM_APP + 43;
    notification.hIcon = LoadIconW(state.instance, MAKEINTRESOURCEW(1));
    if (!notification.hIcon) {
        notification.hIcon = LoadIconW(nullptr, !failed ? IDI_INFORMATION : IDI_ERROR);
    }
    wcsncpy(notification.szTip, L"Kalwer",
            ARRAYSIZE(notification.szTip) - 1);
    if (!state.notification_icon_added) {
        state.notification_icon_added = Shell_NotifyIconW(NIM_ADD, &notification) != FALSE;
    }
    notification.uFlags = NIF_INFO;
    notification.dwInfoFlags = failed ? NIIF_ERROR : NIIF_INFO;
    wcsncpy(notification.szInfoTitle, title.c_str(), ARRAYSIZE(notification.szInfoTitle) - 1);
    wcsncpy(notification.szInfo, body.c_str(), ARRAYSIZE(notification.szInfo) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &notification);
}

void show_job_notification(const CommandJob& job) {
    const bool failed = job.exit_code.load() != 0;
    show_notification(failed ? L"Kalwer command failed" : L"Kalwer command finished",
                      job.command.substr(0, 180) + L"\nexit " + std::to_wstring(job.exit_code.load()), failed);
}

void read_job_output(CommandJob* job) {
    char buffer[4096];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(job->output_read, buffer, sizeof(buffer), &read, nullptr) || !read) break;
        append_job_output(*job, buffer, read);
        PostMessageW(state.window, kCommandChangedMessage,
                     static_cast<WPARAM>(job->id), 0);
    }
}

void wait_for_job(CommandJob* job) {
    WaitForSingleObject(job->process, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(job->process, &exit_code);
    job->exit_code.store(exit_code);
    job->finished_at = std::chrono::steady_clock::now();
    job->running.store(false);

    // ConPTY streams partial lines correctly through a blocking ReadFile, but
    // owns the output pipe until its HPCON is closed. Closing it after the
    // client exits provides EOF; joining the reader then guarantees the final
    // output is visible before the completion notification is posted.
    if (job->pseudo_console) {
        ClosePseudoConsole(job->pseudo_console);
        job->pseudo_console = nullptr;
    }
    if (job->reader.joinable()) job->reader.join();
    PostMessageW(state.window, kCommandChangedMessage,
                 static_cast<WPARAM>(job->id), 1);
}

std::unique_ptr<CommandJob> create_command_job(const std::wstring& command) {
    if (trim_copy(command).empty()) return {};
    auto job = std::make_unique<CommandJob>();
    job->id = ++state.next_job_id;
    job->command = command;

    HANDLE input_read = nullptr;
    HANDLE output_write = nullptr;
    if (!CreatePipe(&input_read, &job->input_write, nullptr, 0) ||
        !CreatePipe(&job->output_read, &output_write, nullptr, 0)) {
        if (input_read) CloseHandle(input_read);
        if (output_write) CloseHandle(output_write);
        if (job->input_write) CloseHandle(job->input_write);
        if (job->output_read) CloseHandle(job->output_read);
        return {};
    }
    HRESULT result = CreatePseudoConsole(COORD{72, 22}, input_read, output_write, 0,
                                         &job->pseudo_console);
    CloseHandle(input_read);
    CloseHandle(output_write);
    if (FAILED(result)) {
        CloseHandle(job->input_write);
        CloseHandle(job->output_read);
        return {};
    }

    SIZE_T attribute_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    auto attributes = static_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0, attribute_size));
    if (!attributes || !InitializeProcThreadAttributeList(attributes, 1, 0,
                                                           &attribute_size) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   job->pseudo_console, sizeof(HPCON), nullptr, nullptr)) {
        if (attributes) HeapFree(GetProcessHeap(), 0, attributes);
        ClosePseudoConsole(job->pseudo_console);
        CloseHandle(job->input_write);
        CloseHandle(job->output_read);
        return {};
    }

    wchar_t command_processor[32768]{};
    ExpandEnvironmentStringsW(L"%ComSpec%", command_processor,
                              static_cast<DWORD>(std::size(command_processor)));
    // Run the command directly under the user's configured command processor.
    // A temporary wrapper combined with CREATE_NO_WINDOW prevented ConPTY from
    // attaching correctly on real Windows: the process lived forever while
    // neither output nor input reached Kalwer. The pseudoconsole is the window,
    // so it must be created with the same flags used by Microsoft's ConPTY path.
    std::wstring command_line = L"\"" + std::wstring(command_processor) +
        L"\" /d /q /c " + command;
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr,
        FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
        nullptr, nullptr, &startup.StartupInfo, &process);
    DeleteProcThreadAttributeList(attributes);
    HeapFree(GetProcessHeap(), 0, attributes);
    if (!created) {
        ClosePseudoConsole(job->pseudo_console);
        CloseHandle(job->input_write);
        CloseHandle(job->output_read);
        return {};
    }
    job->process = process.hProcess;
    job->process_thread = process.hThread;
    job->reader = std::thread(read_job_output, job.get());
    job->waiter = std::thread(wait_for_job, job.get());
    return job;
}

void copy_text_to_clipboard(const std::wstring& text) {
    if (!OpenClipboard(state.window)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* destination = GlobalLock(memory);
        std::memcpy(destination, text.c_str(), bytes);
        GlobalUnlock(memory);
        SetClipboardData(CF_UNICODETEXT, memory);
    }
    CloseClipboard();
}

std::wstring clipboard_text() {
    std::wstring result;
    if (!OpenClipboard(state.window)) return result;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data));
        if (text) {
            result = text;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return result;
}

void open_popup_shell() {
    state.popup_open = true;
    state.popup_opened_at = std::chrono::steady_clock::now();
    state.popup_selection_anchor = 0;
    state.popup_selection_end = 0;
    state.popup_selecting = false;
    state.popup_hover = PopupButton::none;
    state.popup_pressed = PopupButton::none;
    state.render_dirty = true;
    position_and_resize();
    SetFocus(state.edit);
}

void open_popup(const kalwer::PopupDocument& document) {
    state.popup_job = nullptr;
    state.popup_document = document;
    state.popup_document_scroll = 0;
    open_popup_shell();
}

void open_job_popup(CommandJob* job, bool reopened = false) {
    if (!job) return;
    state.popup_document.reset();
    state.popup_job = job;
    job->interacted = reopened;
    open_popup_shell();
}

void start_command_popup(const std::wstring& command) {
    std::unique_ptr<CommandJob> job = create_command_job(command);
    if (!job) return;
    CommandJob* pointer = job.get();
    state.jobs.push_back(std::move(job));
    open_job_popup(pointer);
    // WM_CHAR for the Enter that activated the result is queued after the
    // WM_KEYDOWN handler returns. Do not leak that launch keystroke into a
    // freshly created interactive process (it would instantly accept prompts
    // such as `pause`). All later input is forwarded normally.
    state.suppress_popup_launch_char = true;
}

void write_job_input(const char* data, DWORD size) {
    if (!state.popup_job || !state.popup_job->running.load() ||
        !state.popup_job->input_write) return;
    state.popup_job->interacted = true;
    DWORD written = 0;
    WriteFile(state.popup_job->input_write, data, size, &written, nullptr);
}

void copy_job_output() {
    if (state.popup_document) { copy_text_to_clipboard(wide(state.popup_document->body)); return; }
    if (!state.popup_job) return;
    std::string output;
    {
        std::scoped_lock lock(state.popup_job->output_mutex);
        output = state.popup_job->output;
    }
    state.popup_job->interacted = true;
    copy_text_to_clipboard(wide(output));
}

void close_popup(bool background, bool terminate) {
    CommandJob* job = state.popup_job;
    if (background && job) job->background = true;
    if (terminate && job && job->running.load() && job->process) TerminateProcess(job->process, 130);
    state.popup_open = false;
    state.popup_job = nullptr;
    state.popup_document.reset();
    state.render_dirty = true;
    position_and_resize();
    hide_launcher();
}

void add_background_results(std::vector<AppEntry>& output, const std::wstring& filter) {
    const std::wstring lowered = lower_copy(trim_copy(filter));
    for (auto iterator = state.jobs.rbegin(); iterator != state.jobs.rend(); ++iterator) {
        const CommandJob& job = **iterator;
        if (!job.background) continue;
        if (!lowered.empty() && lower_copy(job.command).find(lowered) == std::wstring::npos) {
            continue;
        }
        AppEntry result;
        result.title = job.running.load()
            ? L"BACKGROUND · RUNNING"
            : L"BACKGROUND · EXIT " + std::to_wstring(job.exit_code.load());
        result.subtitle = job.command;
        result.link = L"::background";
        result.payload = std::to_wstring(job.id);
        output.push_back(std::move(result));
    }
}

bool launch_application(const AppEntry& result) {
    if (!result.app_user_model_id.empty()) {
        ComPtr<IApplicationActivationManager> activation;
        if (SUCCEEDED(CoCreateInstance(CLSID_ApplicationActivationManager, nullptr,
                                       CLSCTX_LOCAL_SERVER,
                                       IID_PPV_ARGS(activation.GetAddressOf())))) {
            DWORD process_id = 0;
            if (SUCCEEDED(activation->ActivateApplication(
                    result.app_user_model_id.c_str(), nullptr, AO_NONE, &process_id))) return true;
        }
    }
    const std::wstring target = result.link.wstring();
    PIDLIST_ABSOLUTE item = nullptr;
    if (SUCCEEDED(SHParseDisplayName(target.c_str(), nullptr, &item, 0, nullptr)) && item) {
        SHELLEXECUTEINFOW execution{};
        execution.cbSize = sizeof(execution);
        execution.fMask = SEE_MASK_IDLIST | SEE_MASK_INVOKEIDLIST;
        execution.hwnd = state.window;
        execution.nShow = SW_SHOWNORMAL;
        execution.lpIDList = item;
        const bool launched = ShellExecuteExW(&execution) != FALSE;
        CoTaskMemFree(item);
        if (launched) return true;
    }
    return reinterpret_cast<INT_PTR>(ShellExecuteW(state.window, L"open", target.c_str(),
                                                    nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

void activate_selection() {
    if (state.results.empty() || window_text(state.edit).substr(0, 1) == L":") return;
    const AppEntry& result = state.results[static_cast<size_t>(state.selection)];
    if (result.link == L"::slash") {
        const auto name = utf8(result.payload);
        if (name == "/help") open_popup(kalwer::help());
        else if (name == "/updates") open_popup({"KALWER UPDATES", "Running v" + utf8(kKalwerVersion) + "\n\n" + update_status.get()});
        else if (name == "/about") open_popup(kalwer::about());
        else if (name == "/exit") DestroyWindow(state.window);
        else if (name == "/settings") {
            state.settings_mode = true; state.selection = state.scroll_offset = 0;
            state.selection_visual = state.scroll_visual = 0; state.render_dirty = true;
        } else if (name == "/reindex") {
            file_index.refresh();
            open_popup({"FILE INDEX", "A file index refresh was requested.\n\nUse :query to search while it runs.\nNew files appear as batches are saved.\n"});
        } else for (const auto& c : kalwer::commands) if (c.name == name) {
            const auto replacement = wide(std::string(c.replacement));
            SetWindowTextW(state.edit, replacement.c_str());
            SendMessageW(state.edit, EM_SETSEL, replacement.size(), replacement.size());
            break;
        }
        return;
    }
    if (result.link == L"::exit") {
        DestroyWindow(state.window);
        return;
    }
    if (result.link == L"::settings") {
        state.settings_mode = true;
        state.selection = 0;
        state.scroll_offset = 0;
        state.selection_visual = 0.0f;
        state.scroll_visual = 0.0f;
        state.render_dirty = true;
        return;
    }
    if (result.link == L"::calculator") {
        copy_text_to_clipboard(result.payload.empty() ? result.title : result.payload);
    } else if (result.link == L"::google") {
        const std::wstring url = L"https://www.google.com/search?q=" +
                                 url_encode(result.subtitle);
        ShellExecuteW(state.window, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (result.link == L"::command") {
        start_command_popup(result.payload.empty() ? result.subtitle : result.payload);
        return;
    } else if (result.link == L"::background") {
        try {
            open_job_popup(find_job(std::stoull(result.payload)), true);
        } catch (...) {
        }
        return;
    } else {
        launch_application(result);
    }
    hide_launcher();
}

void toggle_favorite() {
    if (state.results.empty() || window_text(state.edit).substr(0, 1) == L":") return;
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
    int closing;
    float2 padding;
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
    float reveal_front = closing != 0
        ? ease_out_cubic(opening) * (logical_size.y + 10.0)
        : 88.0 + ease_out_cubic(opening) * (logical_size.y - 86.0);
    float reveal = closing == 0 && pixel_position.y < 88.0
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
    if (FAILED(result)) return result;
    result = render.write_factory->CreateTextFormat(
        family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-US", render.terminal_format.GetAddressOf());
    if (SUCCEEDED(result)) {
        render.terminal_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
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
    render.d2d_context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
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
                          D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
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

    const auto since_input = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - state.last_input_at).count();
    if (state.last_input_at.time_since_epoch().count() && since_input < 110) {
        state.icons_deferred = true;
        return {};
    }

    ComPtr<IShellItem> item;
    HBITMAP image = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(key.c_str(), nullptr,
                                              IID_PPV_ARGS(item.GetAddressOf())))) {
        ComPtr<IShellItemImageFactory> image_factory;
        if (SUCCEEDED(item.As(&image_factory))) {
            image_factory->GetImage(SIZE{96, 96},
                static_cast<SIIGBF>(SIIGBF_BIGGERSIZEOK | SIIGBF_ICONONLY), &image);
        }
    }
    ComPtr<IWICBitmap> wic_bitmap;
    if (image) {
        state.render.wic_factory->CreateBitmapFromHBITMAP(
            image, nullptr, WICBitmapUsePremultipliedAlpha, wic_bitmap.GetAddressOf());
        DeleteObject(image);
    }
    if (!wic_bitmap) {
        SHFILEINFOW information{};
        if (!SHGetFileInfoW(key.c_str(), 0, &information, sizeof(information),
                            SHGFI_ICON | SHGFI_LARGEICON) || !information.hIcon) return {};
        const HRESULT converted = state.render.wic_factory->CreateBitmapFromHICON(
            information.hIcon, wic_bitmap.GetAddressOf());
        DestroyIcon(information.hIcon);
        if (FAILED(converted)) return {};
    }
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

void draw_special_icon(const AppEntry& result, float x, float y) {
    auto& context = state.render.d2d_context;
    const D2D1_COLOR_F bright = color(0.62f, 0.91f, 0.70f, 0.96f);
    const D2D1_COLOR_F dim = color(0.30f, 0.68f, 0.47f, 0.88f);
    fill_round(x, y, x + 38, y + 38, 6.0f, color(0.0f, 0.13f, 0.072f, 0.96f));
    stroke_round(x, y, x + 38, y + 38, 6.0f, 1.2f, dim);
    set_brush(bright);
    if (result.link == L"::calculator") {
        stroke_round(x + 8, y + 5, x + 30, y + 33, 2.0f, 1.5f, bright);
        context->DrawLine(D2D1::Point2F(x + 11, y + 13),
                          D2D1::Point2F(x + 27, y + 13), state.render.brush.Get(), 1.4f);
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                context->FillRectangle(D2D1::RectF(x + 11 + column * 6.0f,
                    y + 18 + row * 4.8f, x + 13.5f + column * 6.0f,
                    y + 20.4f + row * 4.8f), state.render.brush.Get());
            }
        }
    } else if (result.link == L"::settings") {
        context->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 19, y + 19), 10, 10),
                             state.render.brush.Get(), 2.0f);
        context->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 19, y + 19), 3.2f, 3.2f),
                             state.render.brush.Get(), 2.0f);
        for (int index = 0; index < 8; ++index) {
            const float angle = static_cast<float>(index) * 3.14159265f / 4.0f;
            context->DrawLine(D2D1::Point2F(x + 19 + std::cos(angle) * 11.0f,
                                             y + 19 + std::sin(angle) * 11.0f),
                              D2D1::Point2F(x + 19 + std::cos(angle) * 15.0f,
                                             y + 19 + std::sin(angle) * 15.0f),
                              state.render.brush.Get(), 2.2f);
        }
    } else if (result.link == L"::exit") {
        context->DrawLine(D2D1::Point2F(x + 19, y + 7),
                          D2D1::Point2F(x + 19, y + 20), state.render.brush.Get(), 2.3f);
        context->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 19, y + 20), 11, 11),
                             state.render.brush.Get(), 2.0f);
        fill_round(x + 14, y + 5, x + 24, y + 14, 1.0f,
                   color(0.0f, 0.13f, 0.072f, 1.0f));
        set_brush(bright);
        context->DrawLine(D2D1::Point2F(x + 19, y + 6),
                          D2D1::Point2F(x + 19, y + 19), state.render.brush.Get(), 2.4f);
    } else if (result.link == L"::google") {
        context->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 17, y + 17), 8.5f, 8.5f),
                             state.render.brush.Get(), 2.0f);
        context->DrawLine(D2D1::Point2F(x + 23, y + 23),
                          D2D1::Point2F(x + 31, y + 31), state.render.brush.Get(), 2.2f);
    } else {
        stroke_round(x + 6, y + 8, x + 32, y + 30, 1.0f, 1.5f, bright);
        context->DrawLine(D2D1::Point2F(x + 10, y + 14),
                          D2D1::Point2F(x + 15, y + 18), state.render.brush.Get(), 1.7f);
        context->DrawLine(D2D1::Point2F(x + 15, y + 18),
                          D2D1::Point2F(x + 10, y + 22), state.render.brush.Get(), 1.7f);
        context->DrawLine(D2D1::Point2F(x + 18, y + 23),
                          D2D1::Point2F(x + 27, y + 23), state.render.brush.Get(), 1.7f);
    }
}

DWORD search_text_index_at(float logical_x, float logical_y) {
    const std::wstring query = window_text(state.edit);
    if (query.empty()) return 0;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(state.render.write_factory->CreateTextLayout(
            query.c_str(), static_cast<UINT32>(query.size()),
            state.render.input_format.Get(), 548.0f, 31.0f,
            layout.GetAddressOf())) || !layout) return 0;
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    layout->HitTestPoint(logical_x - 67.0f, logical_y - 31.0f,
                         &trailing, &inside, &metrics);
    return static_cast<DWORD>(std::min<UINT32>(
        static_cast<UINT32>(query.size()), metrics.textPosition + (trailing ? 1u : 0u)));
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
    const wchar_t* help = state.settings_mode
        ? L"← → CHANGE   ENTER APPLY   ESC BACK"
        : L"> PTY   < JOBS   ? GOOGLE   ↑↓ SCROLL   ↵ GO";
    draw_text(help, render.tiny_format.Get(), state.settings_mode ? 385.0f : 350.0f,
              58, 625, 72, color(0.46f, 0.67f, 0.52f));

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
    DWORD ghost_start = 0, ghost_end = 0;
    SendMessageW(state.edit, EM_GETSEL, reinterpret_cast<WPARAM>(&ghost_start), reinterpret_cast<LPARAM>(&ghost_end));
    const auto suggestion = suggested_completion();
    if (!query.empty() && ghost_start == query.size() && ghost_end == ghost_start && suggestion.size() > query.size()) {
        DWRITE_TEXT_METRICS metrics{}; layout->GetMetrics(&metrics);
        draw_text(suggestion.substr(query.size()), render.input_format.Get(), text_x + metrics.widthIncludingTrailingWhitespace,
                  text_y, 615, 62, color(0.40f, 0.46f, 0.43f));
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

void draw_settings() {
    auto& render = state.render;
    fill_round(kSearchX, kResultsY - 6, kSearchX + kSearchWidth,
               kLogicalHeight - 4.0f, 12.0f, color(0.0f, 0.075f, 0.043f, 0.94f));
    const std::wstring titles[] = {
        L"START KALWER AT SIGN-IN",
        L"PROMPT RETENTION",
        L"PTY LINE EXTENSION",
        L"PTY VERTICAL EXPANSION",
        L"COMMAND AUTO-CLOSE",
    };
    const std::wstring details[] = {
        L"Current-user startup entry; no administrator access",
        L"Restore the previous query after reopening",
        L"Horizontal connector animation phase",
        L"Rectangle and terminal-content stretch phase",
        L"Delay after an untouched command finishes",
    };
    const std::wstring values[] = {
        autostart_enabled() ? L"ON" : L"OFF",
        std::to_wstring(state.prompt_retention_ms) + L" MS",
        std::to_wstring(state.popup_line_ms) + L" MS",
        std::to_wstring(state.popup_expand_ms) + L" MS",
        std::to_wstring(state.output_close_ms) + L" MS",
    };
    for (int row = 0; row < 5; ++row) {
        const float y = kResultsY + row * kRowPitch;
        fill_round(kResultX, y, kResultX + kResultWidth, y + kRowHeight, 10.0f,
                   color(0.0f, 0.105f, 0.057f, 0.90f));
        stroke_round(kResultX, y, kResultX + kResultWidth, y + kRowHeight, 10.0f,
                     1.0f, color(0.31f, 0.68f, 0.47f, 0.20f));
        draw_text(titles[row], render.title_format.Get(), kResultX + 18, y + 8,
                  kResultX + 445, y + 31, color(0.81f, 0.89f, 0.82f));
        draw_text(details[row], render.subtitle_format.Get(), kResultX + 18, y + 33,
                  kResultX + 475, y + 53, color(0.46f, 0.67f, 0.52f));
        draw_text(values[row], render.title_format.Get(), kResultX + 474, y + 18,
                  kResultX + 583, y + 43, color(0.62f, 0.91f, 0.70f));
    }
}

void draw_results() {
    if (state.settings_mode) {
        draw_settings();
        return;
    }
    auto& render = state.render;
    fill_round(kSearchX, kResultsY - 6, kSearchX + kSearchWidth,
               kLogicalHeight - 4.0f, 12.0f, color(0.0f, 0.075f, 0.043f, 0.88f));
    const int first_drawn = std::max(0, static_cast<int>(std::floor(state.scroll_visual)));
    const int visible_end = std::min(static_cast<int>(state.results.size()),
                                     static_cast<int>(std::ceil(state.scroll_visual)) +
                                         kVisibleResults + 1);
    render.d2d_context->PushAxisAlignedClip(
        D2D1::RectF(kSearchX, kResultsY - 2.0f, kSearchX + kSearchWidth,
                    kLogicalHeight - 4.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (int index = first_drawn; index < visible_end; ++index) {
        const AppEntry& result = state.results[static_cast<size_t>(index)];
        const float display_row = static_cast<float>(index) - state.scroll_visual;
        const float y = kResultsY + display_row * kRowPitch;
        fill_round(kResultX, y, kResultX + kResultWidth, y + kRowHeight, 10.0f,
                   color(0.0f, 0.105f, 0.057f, 0.90f));
        stroke_round(kResultX, y, kResultX + kResultWidth, y + kRowHeight, 10.0f,
                     1.0f, color(0.31f, 0.68f, 0.47f, 0.20f));

        if (result.link.wstring().rfind(L"::", 0) != 0) {
            if (ComPtr<ID2D1Bitmap1> icon = icon_for(result)) {
                render.d2d_context->DrawBitmap(icon.Get(),
                    D2D1::RectF(kResultX + 12, y + 10, kResultX + 50, y + 48),
                    0.94f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            }
        } else {
            draw_special_icon(result, kResultX + 12, y + 10);
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
    render.d2d_context->PopAxisAlignedClip();
    if (state.results.empty()) {
        draw_text(window_text(state.edit).substr(0, 1) == L":" ? file_status : L"NO RESULTS", render.title_format.Get(), 42, kResultsY + 16,
                  400, kResultsY + 44, color(0.30f, 0.68f, 0.47f));
    } else if (state.results.size() > kSelectableResults) {
        const float track_y = kResultsY + 5.0f;
        const float track_height = kVisibleResults * kRowPitch - 14.0f;
        const float fraction = static_cast<float>(kSelectableResults) / state.results.size();
        const float thumb_height = std::max(26.0f, track_height * fraction);
        const int maximum_offset = static_cast<int>(state.results.size()) - kSelectableResults;
        const float thumb_y = track_y + (track_height - thumb_height) *
            (maximum_offset > 0 ? state.scroll_visual / maximum_offset : 0.0f);
        fill_round(kSearchX + kSearchWidth - 7, track_y,
                   kSearchX + kSearchWidth - 4.5f, track_y + track_height, 1.25f,
                   color(0.30f, 0.68f, 0.47f, 0.18f));
        fill_round(kSearchX + kSearchWidth - 7, thumb_y,
                   kSearchX + kSearchWidth - 4.5f, thumb_y + thumb_height, 1.25f,
                   color(0.46f, 0.82f, 0.57f, 0.80f));
    }
}

std::wstring popup_output_text() {
    std::string output;
    if (state.popup_document) {
        std::vector<std::wstring> rows;
        std::wistringstream stream(wide(state.popup_document->body));
        std::wstring line;
        while (std::getline(stream, line)) {
            while (line.size() > 58) {
                size_t split = line.rfind(L' ', 58);
                if (split == std::wstring::npos || split == 0) split = 58;
                if (split && line[split - 1] >= 0xd800 && line[split - 1] <= 0xdbff) --split;
                rows.push_back(line.substr(0, split));
                line.erase(0, split);
                if (!line.empty() && line.front() == L' ') line.erase(0, 1);
            }
            rows.push_back(line);
        }
        state.popup_document_scroll = std::min(state.popup_document_scroll, rows.empty() ? size_t(0) : rows.size() - 1);
        std::wstring visible;
        for (size_t i = state.popup_document_scroll; i < std::min(rows.size(), state.popup_document_scroll + 22); ++i) {
            if (!visible.empty()) visible += L'\n';
            visible += rows[i];
        }
        return visible;
    }
    if (state.popup_job) {
        std::scoped_lock lock(state.popup_job->output_mutex);
        output = state.popup_job->output;
    }
    std::vector<std::string> lines;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) lines.push_back(std::move(line));
    constexpr size_t maximum_lines = 22;
    const size_t first = lines.size() > maximum_lines ? lines.size() - maximum_lines : 0;
    std::string visible;
    for (size_t index = first; index < std::min(lines.size(), first + maximum_lines); ++index) {
        if (index != first) visible.push_back('\n');
        visible += lines[index];
    }
    return wide(visible);
}

UINT32 popup_text_index_at(float logical_x, float logical_y) {
    if (!state.popup_open) return 0;
    const std::wstring output = popup_output_text();
    if (output.empty()) return 0;
    constexpr float panel_left = kLogicalWidth + 28.0f;
    constexpr float panel_right = kExpandedLogicalWidth - 10.0f;
    constexpr float text_top = 94.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(state.render.write_factory->CreateTextLayout(
            output.c_str(), static_cast<UINT32>(output.size()),
            state.render.terminal_format.Get(), panel_right - panel_left - 24.0f,
            366.0f, layout.GetAddressOf())) || !layout) return 0;
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    layout->HitTestPoint(logical_x - panel_left - 12.0f, logical_y - text_top,
                         &trailing, &inside, &metrics);
    return std::min<UINT32>(static_cast<UINT32>(output.size()),
                            metrics.textPosition + (trailing ? 1u : 0u));
}

void copy_popup_selection() {
    if (!state.popup_open) return;
    const std::wstring output = popup_output_text();
    const UINT32 first = std::min(state.popup_selection_anchor,
                                  state.popup_selection_end);
    const UINT32 last = std::min<UINT32>(
        static_cast<UINT32>(output.size()),
        std::max(state.popup_selection_anchor, state.popup_selection_end));
    if (last > first) copy_text_to_clipboard(output.substr(first, last - first));
}

PopupButton popup_button_at(float x, float y) {
    constexpr float panel_right = kExpandedLogicalWidth - 10.0f;
    constexpr float top = 50.0f;
    if (y < top || y > top + 25.0f) return PopupButton::none;
    if (x >= panel_right - 142 && x <= panel_right - 92) return PopupButton::copy;
    if (state.popup_job && x >= panel_right - 86 && x <= panel_right - 49) return PopupButton::background;
    if (x >= panel_right - 43 && x <= panel_right - 10) return PopupButton::close;
    return PopupButton::none;
}

void draw_popup_button(const wchar_t* label, PopupButton button,
                       float left, float right, float top) {
    const bool hovered = state.popup_hover == button;
    const bool pressed = state.popup_pressed == button;
    const float inset = pressed ? 1.0f : 0.0f;
    left += inset;
    right -= inset;
    top += inset;
    fill_round(left, top, right, top + 25.0f - inset, 3.0f,
               pressed ? color(0.12f, 0.42f, 0.245f, 0.99f)
                       : hovered ? color(0.055f, 0.28f, 0.15f, 0.99f)
                                 : color(0.0f, 0.18f, 0.094f, 0.98f));
    stroke_round(left, top, right, top + 25.0f, 3.0f, 1.0f,
                 hovered || pressed ? color(0.62f, 0.91f, 0.70f, 1.0f)
                                    : color(0.46f, 0.82f, 0.57f, 0.92f));
    draw_text(label, state.render.tiny_format.Get(), left + 7.0f, top + 6.0f,
              right - 3.0f, top + 22.0f,
              pressed ? color(0.91f, 1.0f, 0.93f) : color(0.62f, 0.91f, 0.70f));
}

float popup_animation_progress() {
    if (!state.popup_open) return 0.0f;
    const float elapsed = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - state.popup_opened_at).count();
    const float unfold_start = state.popup_line_ms + 170.0f;
    const float value = (elapsed - unfold_start) / std::max(1.0f,
        static_cast<float>(state.popup_expand_ms));
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    const float inverse = 1.0f - clamped;
    return 1.0f - inverse * inverse * inverse;
}

void draw_command_popup() {
    if (!state.popup_open) return;
    auto& render = state.render;
    constexpr float line_origin = kSearchX + kSearchWidth;
    constexpr float panel_left = kLogicalWidth + 28.0f;
    constexpr float panel_right = kExpandedLogicalWidth - 10.0f;
    constexpr float panel_top = 44.0f;
    constexpr float panel_bottom = 472.0f;
    const float elapsed = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - state.popup_opened_at).count();
    auto eased = [](float value) {
        value = std::clamp(value, 0.0f, 1.0f);
        const float inverse = 1.0f - value;
        return 1.0f - inverse * inverse * inverse;
    };
    const float line = eased(elapsed / std::max(1.0f,
        static_cast<float>(state.popup_line_ms)));
    const float detach = eased((elapsed - state.popup_line_ms - 25.0f) / 120.0f);
    const float unfold = popup_animation_progress();
    const float line_start = line_origin + (panel_left - line_origin) * detach;
    const float line_end = line_origin + (panel_right - line_origin) * line;

    set_brush(color(0.46f, 0.82f, 0.57f, 0.98f));
    render.d2d_context->DrawLine(D2D1::Point2F(line_start, panel_top),
                                  D2D1::Point2F(std::max(line_start, line_end), panel_top),
                                  render.brush.Get(), 2.0f);
    if (unfold <= 0.0f) return;

    const float current_bottom = panel_top + (panel_bottom - panel_top) * unfold;
    set_brush(color(0.0f, 0.075f, 0.043f, 0.985f));
    render.d2d_context->FillRectangle(
        D2D1::RectF(panel_left, panel_top, panel_right, current_bottom), render.brush.Get());
    set_brush(color(0.46f, 0.82f, 0.57f, 0.98f));
    render.d2d_context->DrawRectangle(
        D2D1::RectF(panel_left, panel_top, panel_right, current_bottom),
        render.brush.Get(), 1.5f);

    D2D1_MATRIX_3X2_F transform{};
    render.d2d_context->GetTransform(&transform);
    render.d2d_context->PushAxisAlignedClip(
        D2D1::RectF(panel_left + 2, panel_top + 2, panel_right - 2, current_bottom - 2),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    render.d2d_context->SetTransform(
        transform * D2D1::Matrix3x2F::Scale(1.0f, unfold,
                                            D2D1::Point2F(0.0f, panel_top)));
    std::wstring title = state.popup_document ? wide(state.popup_document->title) : L"> " + state.popup_job->command;
    if (title.size() > 34) title = title.substr(0, 33) + L"…";
    draw_text(title, render.tiny_format.Get(), panel_left + 12, panel_top + 10,
              panel_left + 245, panel_top + 30, color(0.81f, 0.89f, 0.82f));
    const std::wstring status = state.popup_document ? L"TEXT" : state.popup_job->running.load() ? L"RUNNING" : L"EXIT " + std::to_wstring(state.popup_job->exit_code.load());
    draw_text(status, render.tiny_format.Get(), panel_left + 246, panel_top + 10,
              panel_left + 315, panel_top + 30, color(0.46f, 0.82f, 0.57f));
    draw_popup_button(L"COPY", PopupButton::copy,
                      panel_right - 142, panel_right - 92, panel_top + 6);
    if (state.popup_job) draw_popup_button(L"BG", PopupButton::background,
                      panel_right - 86, panel_right - 49, panel_top + 6);
    draw_popup_button(L"×", PopupButton::close,
                      panel_right - 43, panel_right - 10, panel_top + 6);
    set_brush(color(0.31f, 0.68f, 0.47f, 0.28f));
    render.d2d_context->DrawLine(D2D1::Point2F(panel_left + 10, panel_top + 39),
                                  D2D1::Point2F(panel_right - 10, panel_top + 39),
                                  render.brush.Get(), 1.0f);
    const std::wstring output = popup_output_text();
    const std::wstring displayed = output.empty() && state.popup_job ? L"STARTING COMMAND…" : output;
    ComPtr<IDWriteTextLayout> output_layout;
    render.write_factory->CreateTextLayout(
        displayed.c_str(), static_cast<UINT32>(displayed.size()),
        render.terminal_format.Get(), panel_right - panel_left - 24.0f,
        panel_bottom - panel_top - 62.0f, output_layout.GetAddressOf());
    if (output_layout) {
        if (!output.empty() && state.popup_selection_anchor != state.popup_selection_end) {
            const UINT32 first = std::min(state.popup_selection_anchor,
                                          state.popup_selection_end);
            const UINT32 last = std::min<UINT32>(
                static_cast<UINT32>(output.size()),
                std::max(state.popup_selection_anchor, state.popup_selection_end));
            UINT32 count = 0;
            output_layout->HitTestTextRange(first, last - first,
                panel_left + 12, panel_top + 50, nullptr, 0, &count);
            std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
            if (count) {
                output_layout->HitTestTextRange(first, last - first,
                    panel_left + 12, panel_top + 50, metrics.data(), count, &count);
                set_brush(color(0.16f, 0.48f, 0.29f, 0.92f));
                for (const auto& metric : metrics) {
                    render.d2d_context->FillRectangle(
                        D2D1::RectF(metric.left, metric.top,
                                    metric.left + metric.width,
                                    metric.top + metric.height), render.brush.Get());
                }
            }
        }
        set_brush(color(0.81f, 0.89f, 0.82f));
        render.d2d_context->DrawTextLayout(
            D2D1::Point2F(panel_left + 12, panel_top + 50), output_layout.Get(),
            render.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    render.d2d_context->SetTransform(transform);
    render.d2d_context->PopAxisAlignedClip();
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
    if (state.closing) {
        const float elapsed = std::chrono::duration<float, std::milli>(
            now - state.closing_at).count();
        opening = 1.0f - std::clamp(elapsed / kCloseDurationMs, 0.0f, 1.0f);
    }
    const float target_selection = static_cast<float>(state.selection);
    const float target_scroll = static_cast<float>(state.scroll_offset);
    float elapsed_ms = state.last_render_at.time_since_epoch().count()
        ? std::chrono::duration<float, std::milli>(now - state.last_render_at).count()
        : 16.0f;
    // An idle launcher can go seconds without a frame. Never feed that idle gap
    // into the interpolator: it used to make the first arrow press snap directly
    // to its destination while subsequent presses animated normally.
    elapsed_ms = std::clamp(elapsed_ms, 1.0f, 24.0f);
    state.last_render_at = now;
    const float response = 1.0f - std::exp(-elapsed_ms / 92.0f);
    state.selection_visual += (target_selection - state.selection_visual) * response;
    state.scroll_visual += (target_scroll - state.scroll_visual) * response;

    render.d2d_context->SetTarget(render.ui_target.Get());
    // The composition surface is sized in physical pixels while every Kalwer
    // layout constant is expressed in logical DIPs. Apply the monitor scale to
    // the finished UI just as the mask shader maps its normalized coordinates
    // back to the logical 650x632 canvas. Keeping these transforms paired is
    // essential: otherwise the rows render at 100% while the GPU outline and
    // halftone render at 125/150%, visibly detaching from the interface.
    render.d2d_context->SetTransform(
        D2D1::Matrix3x2F::Scale(render.scale, render.scale));
    render.d2d_context->BeginDraw();
    render.d2d_context->Clear(color(0, 0, 0, 0));
    draw_search();
    draw_results();
    draw_command_popup();
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
        std::int32_t closing;
        float padding[2];
    } parameters{
        static_cast<float>(state.popup_open ? kExpandedLogicalWidth : kLogicalWidth),
        static_cast<float>(kLogicalHeight),
        opening,
        kResultsY + (state.selection_visual - state.scroll_visual) * kRowPitch,
        state.results.empty() ? 0 : 1,
        state.closing ? 1 : 0,
        {0.0f, 0.0f},
    };
    std::memcpy(mapped.pData, &parameters, sizeof(parameters));
    render.d3d_context->Unmap(render.constants.Get(), 0);
    ID3D11Buffer* constants = render.constants.Get();
    render.d3d_context->PSSetConstantBuffers(0, 1, &constants);
    render.d3d_context->Draw(3, 0);
    ID3D11ShaderResourceView* null_view = nullptr;
    render.d3d_context->PSSetShaderResources(0, 1, &null_view);
    result = render.swap_chain->Present(1, 0);
    const bool popup_animating = state.popup_open && popup_animation_progress() < 0.999f;
    state.render_dirty = state.opening || state.closing || popup_animating ||
                         std::abs(target_selection - state.selection_visual) > 0.01f ||
                         std::abs(target_scroll - state.scroll_visual) > 0.01f;
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
    const int logical_width = state.popup_open ? kExpandedLogicalWidth : kLogicalWidth;
    const int width = static_cast<int>(std::lround(logical_width * scale));
    const int height = static_cast<int>(std::lround(kLogicalHeight * scale));
    const int launcher_width = static_cast<int>(std::lround(kLogicalWidth * scale));
    const int centered = information.rcWork.left +
        ((information.rcWork.right - information.rcWork.left) - launcher_width) / 2;
    const int work_left = static_cast<int>(information.rcWork.left);
    const int work_right = static_cast<int>(information.rcWork.right);
    const int x = std::max(work_left + 8,
        std::min(centered, work_right - width - 8));
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
        std::chrono::duration_cast<std::chrono::milliseconds>(now - state.hidden_at).count() <=
            state.prompt_retention_ms;
    if (!restore) {
        state.settings_mode = false;
        state.restored_query.clear();
        set_window_text_preserving_end(state.edit, L"");
        update_results();
    } else {
        set_window_text_preserving_end(state.edit, state.restored_query);
        update_results();
        state.selection = std::clamp(state.restored_selection, 0,
                                     std::max(0, static_cast<int>(state.results.size()) - 1));
        state.scroll_offset = std::clamp(state.restored_scroll, 0,
            std::max(0, static_cast<int>(state.results.size()) - kSelectableResults));
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
    state.closing = false;
    state.opened_at = now;
    state.last_render_at = now;
    state.selection_visual = static_cast<float>(state.selection);
    state.scroll_visual = static_cast<float>(state.scroll_offset);
    state.render_dirty = true;
    ShowWindow(state.window, SW_SHOWNORMAL);
    SetForegroundWindow(state.window);
    SetFocus(state.edit);
    render_frame();
}

void finish_hide_launcher() {
    ShowWindow(state.window, SW_HIDE);
    state.visible = false;
    state.closing = false;
    state.render_dirty = false;
}

void hide_launcher() {
    if (!state.visible || state.popup_open || state.closing) return;
    state.restored_query = window_text(state.edit);
    state.restored_selection = state.selection;
    state.restored_scroll = state.scroll_offset;
    state.hidden_at = std::chrono::steady_clock::now();
    state.closing = true;
    state.opening = false;
    state.closing_at = state.hidden_at;
    state.render_dirty = true;
}

void cleanup_jobs() {
    for (const auto& job : state.jobs) {
        if (job->running.load() && job->process) TerminateProcess(job->process, 130);
        if (job->input_write) {
            CloseHandle(job->input_write);
            job->input_write = nullptr;
        }
        if (job->waiter.joinable()) job->waiter.join();
        if (job->pseudo_console) ClosePseudoConsole(job->pseudo_console);
        if (job->reader.joinable()) {
            job->reader.join();
        }
        if (job->output_read) CloseHandle(job->output_read);
        if (job->process_thread) CloseHandle(job->process_thread);
        if (job->process) CloseHandle(job->process);
    }
    state.jobs.clear();
    if (state.notification_icon_added) {
        NOTIFYICONDATAW notification{};
        notification.cbSize = sizeof(notification);
        notification.hWnd = state.window;
        notification.uID = 7;
        Shell_NotifyIconW(NIM_DELETE, &notification);
        state.notification_icon_added = false;
    }
}

LRESULT CALLBACK edit_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (state.popup_open && state.popup_document) {
        if (message == WM_KEYDOWN) {
            if (wparam == VK_ESCAPE) close_popup(false, false);
            else if (wparam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                if (GetKeyState(VK_SHIFT) & 0x8000) copy_job_output(); else copy_popup_selection();
            } else if (wparam == VK_DOWN || wparam == VK_NEXT) state.popup_document_scroll += wparam == VK_NEXT ? 18 : 1;
            else if (wparam == VK_UP || wparam == VK_PRIOR) state.popup_document_scroll -= std::min<size_t>(state.popup_document_scroll, wparam == VK_PRIOR ? 18 : 1);
            state.render_dirty = true;
            return 0;
        }
        if (message == WM_CHAR || message == WM_PASTE || message == WM_CUT || message == WM_CLEAR) return 0;
    }
    if (state.popup_open && state.popup_job) {
        if (message == WM_KEYDOWN) {
            state.popup_job->interacted = true;
            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (control) {
                if (wparam == 'A') {
                    const std::wstring output = popup_output_text();
                    state.popup_selection_anchor = 0;
                    state.popup_selection_end = static_cast<UINT32>(output.size());
                    state.render_dirty = true;
                    return 0;
                }
                if (wparam == 'C') {
                    if (shift || state.popup_selection_anchor == state.popup_selection_end) {
                        if (shift) copy_job_output();
                        else write_job_input("\x03", 1);
                    } else {
                        copy_popup_selection();
                    }
                    return 0;
                }
                if (wparam == 'X') {
                    copy_popup_selection();
                    return 0;
                }
                if (wparam == 'V') {
                    const std::string pasted = utf8(clipboard_text());
                    if (!pasted.empty()) write_job_input(
                        pasted.data(), static_cast<DWORD>(pasted.size()));
                    return 0;
                }
            }
            switch (wparam) {
                case VK_ESCAPE: close_popup(false, true); return 0;
                case VK_UP: write_job_input("\x1b[A", 3); return 0;
                case VK_DOWN: write_job_input("\x1b[B", 3); return 0;
                case VK_RIGHT: write_job_input("\x1b[C", 3); return 0;
                case VK_LEFT: write_job_input("\x1b[D", 3); return 0;
                case VK_HOME: write_job_input("\x1b[H", 3); return 0;
                case VK_END: write_job_input("\x1b[F", 3); return 0;
                case VK_DELETE: write_job_input("\x1b[3~", 4); return 0;
                case VK_PRIOR: write_job_input("\x1b[5~", 4); return 0;
                case VK_NEXT: write_job_input("\x1b[6~", 4); return 0;
                case VK_TAB: write_job_input("\t", 1); return 0;
            }
        }
        if (message == WM_CHAR) {
            wchar_t character = static_cast<wchar_t>(wparam);
            if (state.suppress_popup_launch_char) {
                state.suppress_popup_launch_char = false;
                if (character == L'\r' || character == L'\n') return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) &&
                (character == 1 || character == 3 || character == 22 || character == 24)) {
                return 0;
            }
            char encoded[8]{};
            const int length = WideCharToMultiByte(CP_UTF8, 0, &character, 1,
                                                    encoded, sizeof(encoded), nullptr, nullptr);
            if (length > 0) write_job_input(encoded, static_cast<DWORD>(length));
            return 0;
        }
        return CallWindowProcW(state.edit_proc, window, message, wparam, lparam);
    }
    if (message == WM_KEYDOWN) {
        if (state.settings_mode) {
            switch (wparam) {
                case VK_UP: move_selection(-1); return 0;
                case VK_DOWN: move_selection(1); return 0;
                case VK_LEFT: adjust_setting(-1); return 0;
                case VK_RIGHT: adjust_setting(1); return 0;
                case VK_RETURN: adjust_setting(1); return 0;
                case VK_ESCAPE: leave_settings(); return 0;
                default: return 0;
            }
        }
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
            case VK_TAB: {
                const auto suggestion = suggested_completion();
                if (!suggestion.empty()) {
                    SetWindowTextW(state.edit, suggestion.c_str());
                    SendMessageW(state.edit, EM_SETSEL, suggestion.size(), suggestion.size());
                }
                return 0;
            }
        }
    }
    if (state.settings_mode && (message == WM_CHAR || message == WM_PASTE ||
                                message == WM_CUT || message == WM_CLEAR)) return 0;
    if (message == WM_KEYDOWN &&
        (wparam == VK_LEFT || wparam == VK_RIGHT || wparam == VK_HOME ||
         wparam == VK_END ||
         (wparam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)))) {
        // The hidden native edit updates its selection synchronously, but cursor-only
        // movement does not emit EN_CHANGE. Redraw after it has handled the key so the
        // DirectWrite caret and selection follow the native edit on this same frame.
        const LRESULT result = CallWindowProcW(state.edit_proc, window, message,
                                               wparam, lparam);
        state.render_dirty = true;
        return result;
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
                state.last_input_at = std::chrono::steady_clock::now();
                update_results();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_HOTKEY:
            if (wparam == kHotkeyId) {
                if (state.popup_open) {
                    ShowWindow(state.window, SW_SHOWNORMAL);
                    SetForegroundWindow(state.window);
                } else if (state.visible) hide_launcher();
                else show_launcher();
            }
            return 0;
        case kUpdateNoticeMessage: {
            std::unique_ptr<std::wstring> body(reinterpret_cast<std::wstring*>(lparam));
            show_notification(L"Kalwer update", *body, body->find(L"failed") != std::wstring::npos);
            return 0;
        }
        case kToggleMessage:
            if (state.visible) hide_launcher();
            else show_launcher();
            return 0;
        case WM_TIMER:
            poll_files();
            if (state.icons_deferred) {
                const auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - state.last_input_at).count();
                if (idle >= 110) {
                    state.icons_deferred = false;
                    state.render_dirty = true;
                }
            }
            if (state.popup_open && state.popup_job &&
                !state.popup_job->running.load() && !state.popup_job->interacted) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - state.popup_job->finished_at).count();
                if (elapsed >= state.output_close_ms) {
                    close_popup(false, false);
                    return 0;
                }
            }
            if (state.visible && (state.render_dirty || state.opening)) render_frame();
            if (state.closing) {
                const float elapsed = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - state.closing_at).count();
                if (elapsed >= kCloseDurationMs) finish_hide_launcher();
            }
            return 0;
        case kCommandChangedMessage: {
            CommandJob* job = find_job(static_cast<std::uint64_t>(wparam));
            if (job && lparam == 1 && job->background) show_job_notification(*job);
            state.render_dirty = true;
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (state.popup_document) {
                if (GET_WHEEL_DELTA_WPARAM(wparam) < 0) state.popup_document_scroll += 3;
                else state.popup_document_scroll -= std::min<size_t>(3, state.popup_document_scroll);
                state.render_dirty = true; return 0;
            }
            move_selection(GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -1 : 1);
            return 0;
        case WM_MOUSEMOVE: {
            if (!state.visible) break;
            const float scale = state.render.scale;
            const float logical_x = GET_X_LPARAM(lparam) / scale;
            const float logical_y = GET_Y_LPARAM(lparam) / scale;
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            if (state.search_selecting) {
                const DWORD index = search_text_index_at(logical_x, logical_y);
                SendMessageW(state.edit, EM_SETSEL, state.search_selection_anchor, index);
                state.render_dirty = true;
                return 0;
            }
            if (state.popup_selecting) {
                state.popup_selection_end = popup_text_index_at(logical_x, logical_y);
                state.render_dirty = true;
                return 0;
            }
            if (state.popup_open) {
                const PopupButton hover = popup_button_at(logical_x, logical_y);
                if (hover != state.popup_hover) {
                    state.popup_hover = hover;
                    state.render_dirty = true;
                }
            }
            const int row = static_cast<int>((logical_y - kResultsY) / kRowPitch);
            if (!state.popup_open && logical_x >= kResultX &&
                logical_x <= kResultX + kResultWidth && logical_y >= kResultsY &&
                row >= 0 && row < kSelectableResults) {
                const float list_position =
                    (logical_y - kResultsY) / kRowPitch + state.scroll_visual;
                const int index = static_cast<int>(std::floor(list_position));
                const float local_y = (list_position - index) * kRowPitch;
                const bool valid = state.settings_mode
                    ? index >= 0 && index < 5
                    : index >= 0 && index < static_cast<int>(state.results.size()) &&
                          local_y <= kRowHeight;
                if (valid && index != state.selection) {
                    state.selection = index;
                    state.render_dirty = true;
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (state.popup_hover != PopupButton::none) {
                state.popup_hover = PopupButton::none;
                state.render_dirty = true;
            }
            return 0;
        case WM_LBUTTONDOWN: {
            const float scale = state.render.scale;
            const float logical_x = GET_X_LPARAM(lparam) / scale;
            const float logical_y = GET_Y_LPARAM(lparam) / scale;
            if (state.popup_open) {
                if (state.popup_job) state.popup_job->interacted = true;
                state.popup_pressed = popup_button_at(logical_x, logical_y);
                if (state.popup_pressed != PopupButton::none) {
                    SetCapture(window);
                    state.render_dirty = true;
                } else if (logical_x >= kLogicalWidth + 40.0f &&
                           logical_x <= kExpandedLogicalWidth - 22.0f &&
                           logical_y >= 94.0f && logical_y <= 460.0f) {
                    state.popup_selection_anchor = popup_text_index_at(
                        logical_x, logical_y);
                    state.popup_selection_end = state.popup_selection_anchor;
                    state.popup_selecting = true;
                    SetCapture(window);
                    state.render_dirty = true;
                }
                return 0;
            }
            if (logical_x >= 67.0f && logical_x <= 615.0f &&
                logical_y >= 28.0f && logical_y <= 63.0f) {
                SetFocus(state.edit);
                const DWORD index = search_text_index_at(logical_x, logical_y);
                state.search_selection_anchor = index;
                state.search_selecting = true;
                SendMessageW(state.edit, EM_SETSEL, index, index);
                SetCapture(window);
                state.render_dirty = true;
                return 0;
            }
            const int row = static_cast<int>((logical_y - kResultsY) / kRowPitch);
            if (logical_x >= kResultX && logical_x <= kResultX + kResultWidth &&
                logical_y >= kResultsY && row >= 0 && row < kSelectableResults) {
                const float list_position =
                    (logical_y - kResultsY) / kRowPitch + state.scroll_visual;
                const int index = static_cast<int>(std::floor(list_position));
                const float local_y = (list_position - index) * kRowPitch;
                if (state.settings_mode && index < 5) {
                    state.selection = index;
                    adjust_setting(1);
                } else if (index >= 0 && index < static_cast<int>(state.results.size()) &&
                           local_y <= kRowHeight) {
                    state.selection = index;
                    activate_selection();
                }
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (state.search_selecting || state.popup_selecting) {
                state.search_selecting = false;
                state.popup_selecting = false;
                ReleaseCapture();
                state.render_dirty = true;
                return 0;
            }
            if (state.popup_pressed != PopupButton::none) {
                const float scale = state.render.scale;
                const float logical_x = GET_X_LPARAM(lparam) / scale;
                const float logical_y = GET_Y_LPARAM(lparam) / scale;
                const PopupButton pressed = state.popup_pressed;
                const PopupButton released = popup_button_at(logical_x, logical_y);
                state.popup_pressed = PopupButton::none;
                ReleaseCapture();
                state.render_dirty = true;
                if (pressed == released) {
                    if (pressed == PopupButton::copy) copy_job_output();
                    else if (pressed == PopupButton::background) close_popup(true, false);
                    else if (pressed == PopupButton::close) close_popup(false, true);
                }
                return 0;
            }
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wparam) == WA_INACTIVE && state.visible && !state.popup_open &&
                !running_under_wine()) {
                hide_launcher();
            }
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
            cleanup_jobs();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (handle_update_bootstrap()) return 0;
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
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    window_class.hIconSm = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
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
    load_settings();
    load_apps();
    update_results();
    state.hotkey_registered = RegisterHotKey(state.window, kHotkeyId,
                                              MOD_ALT | MOD_NOREPEAT, VK_SPACE);
    if (!state.hotkey_registered) {
        fail_message(L"Alt+Space is already owned by another application.\n"
                     L"Kalwer can still be toggled by launching it again.");
    }
    SetTimer(state.window, kTimerId, 16, nullptr);
    update_window = state.window;
    update_status.set("Running Kalwer v" + utf8(kKalwerVersion) + ". Checking for updates…");
    if (updated_on_launch) announce_update("Update complete. Running Kalwer v" + utf8(kKalwerVersion) + ".");
    std::thread(check_for_update).detach();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (state.mutex) CloseHandle(state.mutex);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
