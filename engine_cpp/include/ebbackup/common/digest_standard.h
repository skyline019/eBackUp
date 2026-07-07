#pragma once

#include <cstddef>
#include <cstdint>

namespace ebbackup {

void Sha256Standard(const uint8_t* data, size_t len, uint8_t hash_out[32]);
void HmacSha256Standard(const uint8_t* key, size_t key_len, const uint8_t* data,
                        size_t data_len, uint8_t out[32]);
void Pbkdf2Sha256Standard(const uint8_t* password, size_t password_len,
                          const uint8_t* salt, size_t salt_len,
                          uint32_t iterations, uint8_t out[32]);
void Pbkdf2Sha256StandardLen(const uint8_t* password, size_t password_len,
                             const uint8_t* salt, size_t salt_len,
                             uint32_t iterations, uint8_t* out, size_t dk_len);

}  // namespace ebbackup
