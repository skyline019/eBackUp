#pragma once

#include <cstddef>
#include <cstdint>

namespace ebbackup {
namespace crypto {

constexpr size_t kAesBlockSize = 16;
constexpr size_t kAes256RoundKeys = 60;

struct Aes256Key {
  uint32_t rk[kAes256RoundKeys];
};

void Aes256KeyExpand(const uint8_t key[32], Aes256Key* out);
void Aes256EncryptBlock(const Aes256Key& key, const uint8_t in[16], uint8_t out[16]);

}  // namespace crypto
}  // namespace ebbackup
