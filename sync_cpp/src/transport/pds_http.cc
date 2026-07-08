#include "ebsync/transport/pds_http.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

#include <sstream>

namespace ebsync {
namespace pdshttp {
namespace {

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  if (len <= 0) return {};
  std::wstring out(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
  if (!out.empty() && out.back() == L'\0') out.pop_back();
  return out;
}

struct ParsedUrl {
  bool https{true};
  std::wstring host;
  INTERNET_PORT port{0};
  std::string path;
};

bool ParseUrl(const std::string& url, ParsedUrl* out) {
  if (!out) return false;
  std::string u = url;
  if (u.find("://") == std::string::npos) u = "https://" + u;
  out->https = u.rfind("https://", 0) == 0;
  const size_t scheme_end = u.find("://") + 3;
  const size_t path_start = u.find('/', scheme_end);
  std::string hostport = path_start == std::string::npos
                             ? u.substr(scheme_end)
                             : u.substr(scheme_end, path_start - scheme_end);
  out->path = path_start == std::string::npos ? "/" : u.substr(path_start);
  INTERNET_PORT port = out->https ? 443 : 80;
  const size_t colon = hostport.find(':');
  if (colon != std::string::npos) {
    out->host = Utf8ToWide(hostport.substr(0, colon));
    port = static_cast<INTERNET_PORT>(std::stoi(hostport.substr(colon + 1)));
  } else {
    out->host = Utf8ToWide(hostport);
  }
  out->port = port;
  return !out->host.empty();
}
#endif

}  // namespace

std::string UrlEncode(const std::string& s) {
  std::ostringstream o;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      o << c;
    } else {
      o << '%' << std::hex << std::uppercase;
      const int hi = (c >> 4) & 0xF;
      const int lo = c & 0xF;
      o << (char)(hi < 10 ? '0' + hi : 'A' + hi - 10);
      o << (char)(lo < 10 ? '0' + lo : 'A' + lo - 10);
      o << std::dec;
    }
  }
  return o.str();
}

HttpResponse Request(const std::string& method, const std::string& url,
                     const std::map<std::string, std::string>& headers,
                     const uint8_t* body, size_t body_len) {
  HttpResponse result;
#ifdef _WIN32
  ParsedUrl parsed;
  if (!ParseUrl(url, &parsed)) {
    result.error = "invalid url";
    return result;
  }
  HINTERNET session =
      WinHttpOpen(L"eb-sync/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    result.error = "WinHttpOpen failed";
    return result;
  }
  HINTERNET connect = WinHttpConnect(session, parsed.host.c_str(), parsed.port, 0);
  if (!connect) {
    WinHttpCloseHandle(session);
    result.error = "WinHttpConnect failed";
    return result;
  }
  const DWORD flags = parsed.https ? WINHTTP_FLAG_SECURE : 0;
  const std::wstring wmethod = Utf8ToWide(method);
  const std::wstring wpath = Utf8ToWide(parsed.path);
  HINTERNET request = WinHttpOpenRequest(connect, wmethod.c_str(), wpath.c_str(),
                                         nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!request) {
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    result.error = "WinHttpOpenRequest failed";
    return result;
  }
  for (const auto& [k, v] : headers) {
    const std::wstring line = Utf8ToWide(k + ": " + v);
    WinHttpAddRequestHeaders(request, line.c_str(), (ULONG)-1L,
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
  }
  const BOOL ok = WinHttpSendRequest(
      request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
      body && body_len ? const_cast<uint8_t*>(body) : WINHTTP_NO_REQUEST_DATA,
      static_cast<DWORD>(body_len), static_cast<DWORD>(body_len), 0);
  if (!ok) {
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    result.error = "WinHttpSendRequest failed";
    result.retryable = true;
    return result;
  }
  if (!WinHttpReceiveResponse(request, nullptr)) {
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    result.error = "WinHttpReceiveResponse failed";
    result.retryable = true;
    return result;
  }
  DWORD status = 0;
  DWORD status_size = sizeof(status);
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                      WINHTTP_NO_HEADER_INDEX);
  result.status = static_cast<int>(status);
  result.ok = status >= 200 && status < 300;
  result.retryable = status >= 500 || status == 408;
  DWORD avail = 0;
  do {
    if (!WinHttpQueryDataAvailable(request, &avail)) break;
    if (avail == 0) break;
    const size_t base = result.body.size();
    result.body.resize(base + avail);
    DWORD read = 0;
    if (!WinHttpReadData(request, result.body.data() + base, avail, &read)) break;
    result.body.resize(base + read);
  } while (avail > 0);
  if (!result.ok) {
    std::ostringstream o;
    o << "HTTP " << status;
    if (!result.body.empty()) {
      o << " ";
      o.write(reinterpret_cast<const char*>(result.body.data()),
              static_cast<std::streamsize>(result.body.size()));
    }
    result.error = o.str();
  }
  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
#else
  (void)method;
  (void)url;
  (void)headers;
  (void)body;
  (void)body_len;
  result.error = "PDS HTTP only supported on Windows";
#endif
  return result;
}

}  // namespace pdshttp
}  // namespace ebsync
