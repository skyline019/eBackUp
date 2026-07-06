#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ebbackup {

void Sha256(const uint8_t* data, size_t len, uint8_t hash_out[32]);
std::string Sha256Hex(const uint8_t* data, size_t len);
std::string Sha256HexString(const std::string& data);
std::string BytesToHex(const uint8_t* data, size_t len);
bool HexToBytes(const std::string& hex, uint8_t* out, size_t out_len);

void HmacSha256(const uint8_t* key, size_t key_len, const uint8_t* data,
                size_t data_len, uint8_t out[32]);
void Pbkdf2Sha256(const uint8_t* password, size_t password_len,
                  const uint8_t* salt, size_t salt_len, uint32_t iterations,
                  uint8_t out[32]);

}  // namespace ebbackup
