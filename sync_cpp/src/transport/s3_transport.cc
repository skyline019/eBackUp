#include "ebsync/transport/transport.h"

#include "ebsync/transport/s3_crypto.h"

#include <chrono>
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace ebsync {
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
  std::string path_prefix;
};

bool ParseEndpoint(const std::string& endpoint, ParsedUrl* out) {
  if (!out) return false;
  std::string url = endpoint;
  if (url.find("://") == std::string::npos) url = "https://" + url;
  out->https = url.rfind("https://", 0) == 0;
  const size_t scheme_end = url.find("://") + 3;
  const size_t path_start = url.find('/', scheme_end);
  std::string hostport =
      path_start == std::string::npos ? url.substr(scheme_end)
                                      : url.substr(scheme_end, path_start - scheme_end);
  if (path_start != std::string::npos) out->path_prefix = url.substr(path_start);
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

std::string NormalizePrefix(std::string p) {
  while (!p.empty() && p.front() == '/') p.erase(p.begin());
  if (!p.empty() && p.back() != '/') p += '/';
  return p;
}

class S3Transport : public IRemoteTransport {
 public:
  S3Transport(S3RemoteConfig cfg, ParsedUrl url)
      : cfg_(std::move(cfg)), url_(std::move(url)) {
    cfg_.prefix = NormalizePrefix(cfg_.prefix);
  }

  std::string FullKey(const std::string& rel) const override {
    return cfg_.prefix + rel;
  }

  TransportResult Head(const std::string& key, bool* exists) override {
    TransportResult r;
    if (!exists) {
      r.error = "exists null";
      return r;
    }
    std::vector<uint8_t> dummy;
    r = Request(L"HEAD", key, nullptr, 0, &dummy);
    if (r.http_status == 404) {
      *exists = false;
      r.ok = true;
      return r;
    }
    *exists = r.ok && r.http_status >= 200 && r.http_status < 300;
    return r;
  }

  TransportResult Put(const std::string& key, const uint8_t* data, size_t len,
                    const PutOptions& opts) override {
    if (opts.skip_if_exists) {
      bool exists = false;
      const TransportResult hr = Head(key, &exists);
      if (hr.ok && exists) {
        TransportResult r;
        r.ok = true;
        r.http_status = 200;
        return r;
      }
    }
    std::vector<uint8_t> out;
    return Request(L"PUT", key, data, len, &out);
  }

  TransportResult Get(const std::string& key, std::vector<uint8_t>* out) override {
    if (!out) {
      TransportResult r;
      r.error = "out null";
      return r;
    }
    return Request(L"GET", key, nullptr, 0, out);
  }

 private:
  TransportResult Request(const wchar_t* method, const std::string& key,
                          const uint8_t* body, size_t body_len,
                          std::vector<uint8_t>* response_out) {
    TransportResult result;
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const std::string amz_date = s3internal::AmzDate(now);
    const std::string date_stamp = s3internal::DateStamp(now);
    const std::string payload_hash =
        body && body_len ? s3internal::Sha256Hex(body, body_len)
                         : s3internal::Sha256Hex("");

    std::string object_key = FullKey(key);
    while (!object_key.empty() && object_key.front() == '/') object_key.erase(0, 1);

    std::string canonical_uri;
    if (cfg_.path_style) {
      canonical_uri = "/" + cfg_.bucket + "/" + object_key;
    } else {
      canonical_uri = "/" + object_key;
    }

    const std::string canonical_headers =
        "host:" + HostHeader() + "\n" + "x-amz-content-sha256:" + payload_hash +
        "\n" + "x-amz-date:" + amz_date + "\n";
    const std::string signed_headers = "host;x-amz-content-sha256;x-amz-date";
    const std::string canonical_request =
        std::string(method == L"HEAD"   ? "HEAD"
                    : method == L"PUT" ? "PUT"
                                       : "GET") +
        "\n" + s3internal::UrlEncode(canonical_uri, false) + "\n\n" + canonical_headers +
        "\n" + signed_headers + "\n" + payload_hash;

    const std::string credential_scope =
        date_stamp + "/" + cfg_.region + "/s3/aws4_request";
    const std::string string_to_sign =
        "AWS4-HMAC-SHA256\n" + amz_date + "\n" + credential_scope + "\n" +
        s3internal::Sha256Hex(canonical_request);

    const std::string k_date = s3internal::HmacSha256Raw("AWS4" + cfg_.secret_key, date_stamp);
    const std::string k_region = s3internal::HmacSha256Raw(k_date, cfg_.region);
    const std::string k_service = s3internal::HmacSha256Raw(k_region, "s3");
    const std::string k_signing = s3internal::HmacSha256Raw(k_service, "aws4_request");
    const std::string signature = s3internal::HmacSha256Hex(k_signing, string_to_sign);

    const std::string auth =
        "AWS4-HMAC-SHA256 Credential=" + cfg_.access_key + "/" + credential_scope +
        ", SignedHeaders=" + signed_headers + ", Signature=" + signature;

    std::wstring request_path;
    if (cfg_.path_style) {
      request_path = Utf8ToWide("/" + cfg_.bucket + "/" + object_key);
    } else {
      request_path = Utf8ToWide("/" + object_key);
      if (!url_.path_prefix.empty() && url_.path_prefix != "/") {
        request_path = Utf8ToWide(url_.path_prefix) + request_path;
      }
    }

    HINTERNET session =
        WinHttpOpen(L"eb-sync/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
      result.error = "WinHttpOpen failed";
      return result;
    }
    HINTERNET connect = WinHttpConnect(session, url_.host.c_str(), url_.port, 0);
    if (!connect) {
      WinHttpCloseHandle(session);
      result.error = "WinHttpConnect failed";
      return result;
    }
    const DWORD flags = url_.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request =
        WinHttpOpenRequest(connect, method, request_path.c_str(), nullptr,
                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
      WinHttpCloseHandle(connect);
      WinHttpCloseHandle(session);
      result.error = "WinHttpOpenRequest failed";
      return result;
    }

    const std::wstring whost = Utf8ToWide(HostHeader());
    const std::wstring wdate = Utf8ToWide(amz_date);
    const std::wstring whash = Utf8ToWide(payload_hash);
    const std::wstring wauth = Utf8ToWide(auth);
    WinHttpAddRequestHeaders(request, (L"Host: " + whost).c_str(), (ULONG)-1L,
                             WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(request, (L"x-amz-date: " + wdate).c_str(), (ULONG)-1L,
                             WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(request, (L"x-amz-content-sha256: " + whash).c_str(),
                             (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(request, (L"Authorization: " + wauth).c_str(), (ULONG)-1L,
                             WINHTTP_ADDREQ_FLAG_ADD);

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
    result.http_status = static_cast<int>(status);
    result.ok = status >= 200 && status < 300;
    result.retryable = status >= 500 || status == 408;

    if (response_out && method == L"GET" && result.ok) {
      DWORD avail = 0;
      do {
        if (!WinHttpQueryDataAvailable(request, &avail)) break;
        if (avail == 0) break;
        const size_t base = response_out->size();
        response_out->resize(base + avail);
        DWORD read = 0;
        if (!WinHttpReadData(request, response_out->data() + base, avail, &read)) break;
        response_out->resize(base + read);
      } while (avail > 0);
    }

    if (!result.ok) {
      std::ostringstream o;
      o << "HTTP " << status;
      result.error = o.str();
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
  }

  std::string HostHeader() const {
    std::string host;
    for (wchar_t c : url_.host) host += static_cast<char>(c <= 127 ? c : '?');
    if ((url_.https && url_.port != 443) || (!url_.https && url_.port != 80)) {
      host += ":" + std::to_string(url_.port);
    }
    if (!cfg_.path_style && !cfg_.bucket.empty()) {
      return cfg_.bucket + "." + host;
    }
    return host;
  }

  S3RemoteConfig cfg_;
  ParsedUrl url_;
};
#endif

}  // namespace

std::unique_ptr<IRemoteTransport> CreateS3Transport(const S3RemoteConfig& cfg) {
#ifdef _WIN32
  if (cfg.endpoint.empty() || cfg.bucket.empty() || cfg.access_key.empty() ||
      cfg.secret_key.empty()) {
    return nullptr;
  }
  ParsedUrl url;
  if (!ParseEndpoint(cfg.endpoint, &url)) return nullptr;
  return std::make_unique<S3Transport>(cfg, url);
#else
  (void)cfg;
  return nullptr;
#endif
}

}  // namespace ebsync
