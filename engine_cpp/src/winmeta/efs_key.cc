#include "ebbackup/winmeta/efs_key.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>
#include <cstring>
#include <algorithm>

#include "ebbackup/common/path_encoding.h"

namespace ebbackup {
namespace winmeta {
namespace {

std::string Base64Encode(const uint8_t* data, size_t len) {
  static const char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                       ((i + 1 < len ? data[i + 1] : 0) << 8) |
                       (i + 2 < len ? data[i + 2] : 0);
    out.push_back(kAlphabet[(n >> 18) & 63]);
    out.push_back(kAlphabet[(n >> 12) & 63]);
    out.push_back(i + 1 < len ? kAlphabet[(n >> 6) & 63] : '=');
    out.push_back(i + 2 < len ? kAlphabet[n & 63] : '=');
  }
  return out;
}

std::vector<uint8_t> Base64Decode(const std::string& in) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::vector<uint8_t> out;
  int buf = 0;
  int bits = 0;
  for (char c : in) {
    if (c == '=' || c == '\r' || c == '\n') continue;
    const int v = val(c);
    if (v < 0) continue;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
    }
  }
  return out;
}

struct EfsExportContext {
  std::vector<uint8_t> raw;
};

DWORD WINAPI EfsExportCallback(PBYTE pbData, PVOID pvCallbackContext, ULONG ulLength) {
  auto* ctx = static_cast<EfsExportContext*>(pvCallbackContext);
  if (!ctx) return ERROR_INVALID_PARAMETER;
  if (pbData == nullptr || ulLength == 0) return ERROR_SUCCESS;
  ctx->raw.insert(ctx->raw.end(), pbData, pbData + ulLength);
  return ERROR_SUCCESS;
}

struct EfsImportContext {
  const uint8_t* data{nullptr};
  size_t size{0};
  size_t offset{0};
};

DWORD WINAPI EfsImportCallback(PBYTE pbData, PVOID pvCallbackContext, PULONG pulLength) {
  auto* ctx = static_cast<EfsImportContext*>(pvCallbackContext);
  if (!ctx || !pulLength) return ERROR_INVALID_PARAMETER;
  if (!pbData) {
    *pulLength = 0;
    return ERROR_SUCCESS;
  }
  if (*pulLength == 0) return ERROR_SUCCESS;
  const size_t remain = ctx->size > ctx->offset ? ctx->size - ctx->offset : 0;
  const ULONG cap = *pulLength;
  const ULONG to_copy = remain >= cap ? cap : static_cast<ULONG>(remain);
  if (to_copy > 0) {
    std::memcpy(pbData, ctx->data + ctx->offset, to_copy);
    ctx->offset += to_copy;
  }
  *pulLength = to_copy;
  return ERROR_SUCCESS;
}

}  // namespace

Status ExportEfsKeyBlob(const std::string& path_utf8, std::string* out_b64) {
  if (!out_b64) return Status::InvalidArgument("out_b64 is null");
  out_b64->clear();

  const std::wstring wide = Utf8ToWide(path_utf8);
  const DWORD attrs = GetFileAttributesW(wide.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_ENCRYPTED) == 0) {
    return Status::InvalidArgument("not an EFS encrypted file");
  }

  EfsExportContext ctx;
  if (!ReadEncryptedFileRaw(EfsExportCallback, &ctx, static_cast<PVOID>(const_cast<wchar_t*>(wide.c_str())))) {
    return Status::IoError("ReadEncryptedFileRaw failed");
  }
  if (ctx.raw.empty()) {
    return Status::IoError("efs key export returned empty blob");
  }
  *out_b64 = Base64Encode(ctx.raw.data(), ctx.raw.size());
  return Status::Ok();
}

Status ImportEfsKeyBlob(const std::string& path_utf8, const std::string& b64) {
  if (b64.empty()) return Status::InvalidArgument("empty efs key blob");

  const std::vector<uint8_t> raw = Base64Decode(b64);
  if (raw.empty()) return Status::Corrupt("invalid efs key blob base64");

  const std::wstring wide = Utf8ToWide(path_utf8);
  EfsImportContext ctx{};
  ctx.data = raw.data();
  ctx.size = raw.size();

  if (!WriteEncryptedFileRaw(EfsImportCallback, &ctx,
                             static_cast<PVOID>(const_cast<wchar_t*>(wide.c_str())))) {
    return Status::IoError("WriteEncryptedFileRaw failed");
  }
  if (ctx.offset != ctx.size) {
    return Status::IoError("WriteEncryptedFileRaw incomplete");
  }
  return Status::Ok();
}

}  // namespace winmeta
}  // namespace ebbackup

#endif
