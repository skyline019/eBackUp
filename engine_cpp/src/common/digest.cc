#include "ebbackup/common/digest.h"

#include <cctype>
#include <cstring>

#include "ebbackup/common/digest_legacy.h"
#include "ebbackup/common/digest_sha_ni.h"
#include "ebbackup/common/digest_standard.h"

namespace ebbackup {

void ContentHash(DigestAlgo algo, const uint8_t* data, size_t len,
                 uint8_t hash_out[32]) {
  if (algo == DigestAlgo::kStandard) {
    if (DigestShaNiAvailable()) {
      Sha256ShaNi(data, len, hash_out);
    } else {
      Sha256Standard(data, len, hash_out);
    }
  } else {
    Sha256Legacy(data, len, hash_out);
  }
}

void HmacSha256(DigestAlgo algo, const uint8_t* key, size_t key_len,
                const uint8_t* data, size_t data_len, uint8_t out[32]) {
  if (algo == DigestAlgo::kStandard) {
    HmacSha256Standard(key, key_len, data, data_len, out);
  } else {
    HmacSha256Legacy(key, key_len, data, data_len, out);
  }
}

void Pbkdf2Sha256(DigestAlgo algo, const uint8_t* password, size_t password_len,
                  const uint8_t* salt, size_t salt_len, uint32_t iterations,
                  uint8_t out[32]) {
  if (algo == DigestAlgo::kStandard) {
    Pbkdf2Sha256Standard(password, password_len, salt, salt_len, iterations,
                         out);
  } else {
    Pbkdf2Sha256Legacy(password, password_len, salt, salt_len, iterations, out);
  }
}

void Sha256(const uint8_t* data, size_t len, uint8_t hash_out[32]) {
  ContentHash(DigestAlgo::kLegacy, data, len, hash_out);
}

std::string BytesToHex(const uint8_t* data, size_t len) {
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out.push_back(kHex[(data[i] >> 4) & 0xF]);
    out.push_back(kHex[data[i] & 0xF]);
  }
  return out;
}

std::string Sha256Hex(const uint8_t* data, size_t len) {
  uint8_t hash[32];
  Sha256(data, len, hash);
  return BytesToHex(hash, 32);
}

std::string Sha256HexString(const std::string& data) {
  return Sha256Hex(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

bool HexToBytes(const std::string& hex, uint8_t* out, size_t out_len) {
  if (hex.size() != out_len * 2) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < out_len; ++i) {
    const int hi = nibble(hex[i * 2]);
    const int lo = nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

}  // namespace ebbackup
