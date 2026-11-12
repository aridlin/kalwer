#pragma once
#include "../notes_commands.hpp"
#include "../vendor/json/json.hpp"
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <regex>
#include <shlobj.h>
#include <wincrypt.h>
#include <windows.h>
#include <winhttp.h>
namespace kalwer::winservices {
using Json = nlohmann::json;
namespace fs = std::filesystem;
inline std::wstring wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                              int(s.size()), nullptr, 0);
  std::wstring out(n, L' ');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()),
                      out.data(), n);
  return out;
}
inline std::string utf8(const std::wstring &s) {
  if (s.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0,
                              nullptr, nullptr);
  std::string out(n, ' ');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n,
                      nullptr, nullptr);
  return out;
}
inline fs::path data() {
  PWSTR p = nullptr;
  fs::path out = L".";
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
    out = p;
    CoTaskMemFree(p);
  }
  out /= L"Kalwer";
  fs::create_directories(out);
  return out;
}
inline std::string protect(const std::string &input, bool encrypt) {
  auto dll = LoadLibraryW(L"crypt32.dll");
  if (!dll)
    throw std::runtime_error("Private settings unavailable.");
  DATA_BLOB in{DWORD(input.size()),
               reinterpret_cast<BYTE *>(const_cast<char *>(input.data()))},
      out{};
  BOOL ok = FALSE;
  if (encrypt) {
    using Fn = BOOL(WINAPI *)(DATA_BLOB *, LPCWSTR, DATA_BLOB *, PVOID,
                              CRYPTPROTECT_PROMPTSTRUCT *, DWORD, DATA_BLOB *);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(dll, "CryptProtectData"));
    if (fn)
      ok = fn(&in, L"Kalwer", nullptr, nullptr, nullptr,
              CRYPTPROTECT_UI_FORBIDDEN, &out);
  } else {
    using Fn = BOOL(WINAPI *)(DATA_BLOB *, LPWSTR *, DATA_BLOB *, PVOID,
                              CRYPTPROTECT_PROMPTSTRUCT *, DWORD, DATA_BLOB *);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(dll, "CryptUnprotectData"));
    if (fn)
      ok = fn(&in, nullptr, nullptr, nullptr, nullptr,
              CRYPTPROTECT_UI_FORBIDDEN, &out);
  }
  FreeLibrary(dll);
  if (!ok)
    throw std::runtime_error(
        "Cannot access private settings for this Windows account.");
  std::string value(reinterpret_cast<char *>(out.pbData), out.cbData);
  LocalFree(out.pbData);
  return value;
}
inline void atomic_save(const fs::path &path, const std::string &value) {
  fs::create_directories(path.parent_path());
  auto temp = path;
  temp += L".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(value.data(), value.size());
    if (!out)
      throw std::runtime_error("Cannot save settings.");
  }
  if (!MoveFileExW(temp.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Cannot save settings.");
}
inline std::string read(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
inline std::string secret(const std::wstring &name) {
  auto bytes = read(data() / name);
  return bytes.empty() ? "" : protect(bytes, false);
}
inline void secret(const std::wstring &name, const std::string &value) {
  atomic_save(data() / name, protect(value, true));
}
inline std::string encode(const std::string &s) {
  std::string out;
  constexpr char hex[] = "0123456789ABCDEF";
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      out += char(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}
struct Http {
  HINTERNET session = nullptr;
  std::atomic<bool> cancelled = false;
  std::mutex mutex;
  HINTERNET active = nullptr;
  std::map<std::wstring, std::wstring> cookies;
  Http() {
    session = WinHttpOpen(L"Kalwer", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          nullptr, nullptr, 0);
    if (!session)
      throw std::runtime_error("Network client unavailable.");
    WinHttpSetTimeouts(session, 8000, 8000, 15000, 30000);
  }
  ~Http() {
    cancel();
    if (session)
      WinHttpCloseHandle(session);
  }
  void cancel() {
    cancelled = true;
    std::lock_guard lock(mutex);
    if (active) {
      WinHttpCloseHandle(active);
      active = nullptr;
    }
  }
  void check() {
    if (cancelled)
      throw std::runtime_error("Cancelled.");
  }
  std::string
  request(const std::string &address, const std::string &method = "GET",
          const std::string &body = {}, const std::string &headers = {},
          const fs::path &file = {}, std::uint64_t limit = 2 * 1024 * 1024,
          const std::string &prefix = {}, const std::string &suffix = {},
          const fs::path &destination = {}) {
    check();
    auto url = wide(address);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        DWORD(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != 443 ||
        parts.dwUserNameLength || parts.dwPasswordLength)
      throw std::runtime_error("Invalid service address.");
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength),
        path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
      path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (host != L"memes.aridlin.pl" && host != L"bin.aridlin.pl" &&
        host != L"notes.aridlin.pl")
      throw std::runtime_error("Invalid service host.");
    auto connection = WinHttpConnect(session, host.c_str(), 443, 0);
    if (!connection)
      throw std::runtime_error("Connection failed.");
    auto verb = wide(method);
    auto request = WinHttpOpenRequest(
        connection, verb.c_str(), path.c_str(), nullptr, nullptr,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
      WinHttpCloseHandle(connection);
      throw std::runtime_error("Request failed.");
    }
    {
      std::lock_guard lock(mutex);
      active = request;
    }
    auto cleanup = [&] {
      std::lock_guard lock(mutex);
      if (active == request) {
        WinHttpCloseHandle(request);
        active = nullptr;
      }
      WinHttpCloseHandle(connection);
    };
    try {
      DWORD features = WINHTTP_DISABLE_COOKIES;
      WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &features,
                       sizeof(features));
      DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
      WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                       sizeof(policy));
      auto hs = wide(headers);
      if (!cookies[host].empty())
        hs += L"Cookie: " + cookies[host] + L"\r\n";
      std::uint64_t size =
          file.empty() ? body.size()
                       : fs::file_size(file) + prefix.size() + suffix.size();
      if (size > DWORD(-1))
        throw std::runtime_error("File is too large.");
      if (!WinHttpSendRequest(
              request, hs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hs.c_str(),
              DWORD(hs.size()), nullptr, 0, DWORD(size), 0))
        throw std::runtime_error("Network request failed.");
      auto write = [&](const char *bytes, size_t count) {
        while (count) {
          check();
          DWORD written = 0;
          if (!WinHttpWriteData(request, bytes,
                                DWORD(std::min<size_t>(count, 32768)),
                                &written) ||
              !written)
            throw std::runtime_error(
                "Upload failed. Check the destination before retrying.");
          bytes += written;
          count -= written;
        }
      };
      if (file.empty())
        write(body.data(), body.size());
      else {
        write(prefix.data(), prefix.size());
        std::ifstream in(file, std::ios::binary);
        std::array<char, 32768> chunk;
        while (in) {
          in.read(chunk.data(), chunk.size());
          write(chunk.data(), size_t(in.gcount()));
        }
        if (!in.eof())
          throw std::runtime_error("Cannot read file.");
        write(suffix.data(), suffix.size());
      }
      if (!WinHttpReceiveResponse(request, nullptr))
        throw std::runtime_error(
            "No server response. Check the destination before retrying.");
      DWORD status = 0, n = sizeof(status);
      WinHttpQueryHeaders(request,
                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          nullptr, &status, &n, nullptr);
      wchar_t cookie[4096];
      n = sizeof(cookie);
      if (WinHttpQueryHeaders(request, WINHTTP_QUERY_SET_COOKIE, nullptr,
                              cookie, &n, nullptr)) {
        std::wstring value(cookie);
        cookies[host] = value.substr(0, value.find(L';'));
      }
      std::string result;
      std::ofstream output;
      if (!destination.empty() && status >= 200 && status < 300) {
        output.open(destination, std::ios::binary | std::ios::trunc);
        if (!output)
          throw std::runtime_error("Cannot save download.");
      }
      std::uint64_t received = 0;
      std::array<char, 32768> buffer;
      DWORD got = 0;
      while (true) {
        if (!WinHttpReadData(request, buffer.data(), buffer.size(), &got))
          throw std::runtime_error("Download interrupted.");
        if (!got)
          break;
        check();
        received += got;
        if (received > limit)
          throw std::runtime_error("Response is too large.");
        if (output.is_open()) {
          output.write(buffer.data(), got);
          if (!output)
            throw std::runtime_error("Cannot save download.");
        } else
          result.append(buffer.data(), got);
      }
      check();
      if (status < 200 || status >= 300) {
        std::string message =
            "Server rejected this request (" + std::to_string(status) + ").";
        try {
          message = Json::parse(result).value("error", message);
        } catch (...) {
        }
        throw std::runtime_error(message);
      }
      cleanup();
      return result;
    } catch (...) {
      cleanup();
      throw;
    }
  }
};
inline std::string media(std::string path, bool meme) {
  auto base = meme ? "https://memes.aridlin.pl/" : "https://bin.aridlin.pl";
  if (meme && (path.starts_with("storage/") || path.starts_with("kalwer.php?")))
    path = base + path;
  if (!meme && path.starts_with("/file/"))
    path = base + path;
  if (!(meme ? (path.starts_with(std::string(base) + "storage/") ||
                path.starts_with(std::string(base) + "kalwer.php?"))
             : path.starts_with(std::string(base) + "/file/")))
    throw std::runtime_error("Invalid media address.");
  return path;
}
inline std::string hash(const std::string &value) {
  BCRYPT_ALG_HANDLE alg{};
  BCRYPT_HASH_HANDLE hash{};
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) <
      0)
    throw std::runtime_error("Hash unavailable.");
  DWORD size = 0, used = 0;
  BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                    sizeof(size), &used, 0);
  std::vector<unsigned char> object(size);
  std::array<unsigned char, 32> digest{};
  auto ok =
      BCryptCreateHash(alg, &hash, object.data(), size, nullptr, 0, 0) >= 0 &&
      BCryptHashData(hash,
                     reinterpret_cast<PUCHAR>(const_cast<char *>(value.data())),
                     ULONG(value.size()), 0) >= 0 &&
      BCryptFinishHash(hash, digest.data(), digest.size(), 0) >= 0;
  if (hash)
    BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  if (!ok)
    throw std::runtime_error("Hash failed.");
  std::string out;
  for (auto c : digest) {
    out += "0123456789abcdef"[c >> 4];
    out += "0123456789abcdef"[c & 15];
  }
  return out;
}
inline fs::path cached_path(const std::string &url, const std::string &name) {
  auto directory = data() / L"service-media";
  fs::create_directories(directory);
  auto ext = fs::path(wide(name)).extension().wstring();
  if (ext.size() > 10 ||
      ext.find_first_not_of(
          L".abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") !=
          ext.npos)
    ext = L".bin";
  return directory / (wide(hash(url)) + ext);
}
inline fs::path download(Http &http, const std::string &url,
                         const std::string &name,
                         std::uint64_t limit = 200 * 1024 * 1024) {
  auto path = cached_path(url, name);
  if (fs::is_regular_file(path))
    return path;
  auto temporary = path;
  temporary += L"." + std::to_wstring(GetCurrentThreadId()) + L".part";
  try {
    http.request(url, "GET", {}, {}, {}, limit, {}, {}, temporary);
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING))
      throw std::runtime_error("Cannot save download.");
  } catch (...) {
    std::error_code ec;
    fs::remove(temporary, ec);
    throw;
  }
  return path;
}
inline Json memes(Http &http, const std::string &q, const std::string &cat,
                  int offset) {
  return Json::parse(http.request(
      "https://memes.aridlin.pl/kalwer.php?action=list&q=" + encode(q) +
      "&cat=" + encode(cat) + "&offset=" + std::to_string(offset)));
}
inline std::string upload_meme(Http &http, const fs::path &file,
                               const std::string &category,
                               const std::string &name, bool validate = false) {
  auto key = secret(L"memes-upload-key.dpapi");
  if (!std::regex_match(key, std::regex("[a-f0-9]{64}")))
    throw std::runtime_error("Save a valid private upload key first.");
  auto size = fs::file_size(file);
  if (!size || size > 200 * 1024 * 1024)
    throw std::runtime_error("Memes allows files up to 200 MiB.");
  auto boundary =
      "kalwer" + hash(std::to_string(GetTickCount64())).substr(0, 24);
  std::string prefix;
  for (auto field : std::vector<std::pair<std::string, std::string>>{
           {"category", category},
           {"name", name},
           {"validate_only", validate ? "1" : "0"}})
    prefix += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" +
              field.first + "\"\r\n\r\n" + field.second + "\r\n";
  auto filename = utf8(file.filename().wstring());
  std::replace_if(
      filename.begin(), filename.end(),
      [](char c) { return c == '"' || c == '\r' || c == '\n'; }, '_');
  prefix += "--" + boundary +
            "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" +
            filename + "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
  auto reply = Json::parse(http.request(
      "https://memes.aridlin.pl/kalwer.php?action=upload", "POST", {},
      "X-Kalwer-Upload-Key: " + key +
          "\r\nContent-Type: multipart/form-data; boundary=" + boundary +
          "\r\n",
      file, 2 * 1024 * 1024, prefix, "\r\n--" + boundary + "--\r\n"));
  return validate ? "validated"
                  : media(reply.at("url").get<std::string>(), true);
}
inline std::string mime(const fs::path &file) {
  auto extension = utf8(file.extension().wstring());
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  static const std::map<std::string, std::string> types = {
      {".png", "image/png"},         {".jpg", "image/jpeg"},
      {".jpeg", "image/jpeg"},       {".gif", "image/gif"},
      {".webp", "image/webp"},       {".bmp", "image/bmp"},
      {".mp4", "video/mp4"},         {".webm", "video/webm"},
      {".mov", "video/quicktime"},   {".mp3", "audio/mpeg"},
      {".wav", "audio/wav"},         {".txt", "text/plain"},
      {".md", "text/plain"},         {".csv", "text/csv"},
      {".json", "application/json"}, {".pdf", "application/pdf"},
      {".zip", "application/zip"}};
  auto found = types.find(extension);
  return found == types.end() ? "application/octet-stream" : found->second;
}
inline std::string upload_bin(Http &http, const std::vector<fs::path> &files) {
  if (files.empty() || files.size() > 16)
    throw std::runtime_error("Choose 1–16 files.");
  auto html = http.request("https://bin.aridlin.pl/");
  auto field = [&](const char *name) {
    std::smatch found;
    return std::regex_search(html, found,
                             std::regex(std::string("name=\"") + name +
                                        "\" value=\"([^\"]*)\""))
               ? found[1].str()
               : std::string{};
  };
  auto csrf = field("csrf"), revision = field("revision");
  if (csrf.size() != 64)
    throw std::runtime_error("Cannot start upload session.");
  Json body = {{"action", revision.empty() ? "replace" : "append"},
               {"revision", revision},
               {"items", Json::array()}};
  std::uint64_t total = 0;
  for (auto &f : files) {
    auto size = fs::file_size(f);
    total += size;
    if (size > 1073741824ULL || total > 2147483648ULL)
      throw std::runtime_error(
          "Binup allows 1 GiB per file and 2 GiB per paste.");
    body["items"].push_back({{"name", utf8(f.filename().wstring())},
                             {"mime", mime(f)},
                             {"size", size}});
  }
  auto headers = "X-CSRF-Token: " + csrf + "\r\n";
  auto id =
      Json::parse(http.request("https://bin.aridlin.pl/uploads", "POST",
                               body.dump(),
                               headers + "Content-Type: application/json\r\n"))
          .at("id")
          .get<std::string>();
  if (!std::regex_match(id, std::regex("[a-f0-9]{48}")))
    throw std::runtime_error("Invalid upload session.");
  auto session = "https://bin.aridlin.pl/uploads/" + id;
  try {
    std::string chunk(4 * 1024 * 1024, ' ');
    for (size_t i = 0; i < files.size(); i++) {
      std::ifstream in(files[i], std::ios::binary);
      std::uint64_t offset = 0;
      while (in) {
        in.read(chunk.data(), chunk.size());
        auto count = in.gcount();
        if (!count)
          break;
        std::string answer;
        for (int attempt = 0;; attempt++)
          try {
            answer = http.request(
                session + "/" + std::to_string(i) +
                    "?offset=" + std::to_string(offset),
                "PUT", chunk.substr(0, count),
                headers + "Content-Type: application/octet-stream\r\n");
            break;
          } catch (...) {
            http.check();
            if (attempt)
              throw;
          }
        offset += count;
        if (Json::parse(answer).at("offset").get<std::uint64_t>() != offset)
          throw std::runtime_error("Chunk was not accepted.");
      }
    }
    auto committed =
        Json::parse(http.request(session + "/commit", "POST", {}, headers));
    auto rows = Json::parse(http.request("https://bin.aridlin.pl/items"));
    std::string links;
    for (size_t i = rows.size() > files.size() ? rows.size() - files.size() : 0;
         i < rows.size(); i++) {
      auto url = media(rows[i].at("url").get<std::string>(), false);
      if (!links.empty())
        links += '\n';
      links += url;
    }
    return links.empty() ? "https://bin.aridlin.pl/" : links;
  } catch (...) {
    if (!http.cancelled)
      try {
        http.request(session, "DELETE", {}, headers);
      } catch (...) {
      }
    throw;
  }
}
} // namespace kalwer::winservices
