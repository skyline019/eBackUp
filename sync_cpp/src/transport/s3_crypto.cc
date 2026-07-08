#include "ebsync/transport/s3_crypto.h"

#include <cctype>
#include <cstdio>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace ebsync {
namespace s3internal {
namespace {

std::string ToHex(const uint8_t* data, size_t len) {
  std::ostringstream o;
  for (size_t i = 0; i < len; ++i) {
    o << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
  }
  return o.str();
}

#ifdef _WIN32
bool Sha256Raw(const uint8_t* data, size_t len, uint8_t out[32]) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD hash_len = 0;
  DWORD cb = 0;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
    return false;
  }
  if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_len),
                        sizeof(hash_len), &cb, 0) != 0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return false;
  }
  if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) != 0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return false;
  }
  if (BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(len), 0) != 0) {
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return false;
  }
  if (BCryptFinishHash(hash, out, hash_len, 0) != 0) {
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return false;
  }
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  return true;
}

bool HmacSha256Raw(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len,
                   uint8_t out[32]) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD hash_len = 0;
  DWORD cb = 0;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) {
    return false;
  }
  if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_len),
                        sizeof(hash_len), &cb, 0) != 0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return false;
  }
  if (BCryptCreateHash(alg, &hash, nullptr, 0, const_cast<PUCHAR>(key),
                       static_cast<ULONG>(key_len), 0) != 0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return false;
  }
  if (BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(data_len), 0) != 0) {
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return false;
  }
  if (BCryptFinishHash(hash, out, hash_len, 0) != 0) {
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return false;
  }
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  return true;
}
#endif

}  // namespace

std::string Sha256Hex(const uint8_t* data, size_t len) {
#ifdef _WIN32
  uint8_t raw[32];
  if (!Sha256Raw(data, len, raw)) return {};
  return ToHex(raw, 32);
#else
  (void)data;
  (void)len;
  return {};
#endif
}

std::string Sha256Hex(const std::string& data) {
  return Sha256Hex(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string HmacSha256Raw(const std::string& key, const std::string& data) {
#ifdef _WIN32
  uint8_t raw[32];
  if (!HmacSha256Raw(reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                     reinterpret_cast<const uint8_t*>(data.data()), data.size(), raw)) {
    return {};
  }
  return std::string(reinterpret_cast<char*>(raw), 32);
#else
  (void)key;
  (void)data;
  return {};
#endif
}

std::string HmacSha256Hex(const std::string& key, const std::string& data) {
  const std::string raw = HmacSha256Raw(key, data);
  if (raw.size() != 32) return {};
  return ToHex(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
}

std::string UrlEncode(const std::string& in, bool encode_slash) {
  std::ostringstream o;
  for (unsigned char c : in) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' ||
        (!encode_slash && c == '/')) {
      o << c;
    } else {
      o << '%' << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(c) << std::dec;
    }
  }
  return o.str();
}

std::string AmzDate(int64_t unix_time) {
  std::time_t t = static_cast<std::time_t>(unix_time);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02dZ", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

std::string DateStamp(int64_t unix_time) {
  std::time_t t = static_cast<std::time_t>(unix_time);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                tm.tm_mday);
  return buf;
}

}  // namespace s3internal
}  // namespace ebsync
