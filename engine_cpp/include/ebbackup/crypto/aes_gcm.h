#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {
namespace crypto {

constexpr size_t kAesGcmNonceSize = 12;
constexpr size_t kAesGcmTagSize = 16;
constexpr size_t kContentKeySize = 32;

Status Aes256GcmEncrypt(const uint8_t key[32], const uint8_t* plain,
                        size_t plain_len, std::vector<uint8_t>* out);

Status Aes256GcmDecrypt(const uint8_t key[32], const uint8_t* blob,
                        size_t blob_len, std::vector<uint8_t>* out);

Status LoadOrCreateRepoSalt(const std::string& repo_path, uint8_t salt[16]);
Status DeriveContentKey(const std::string& password, const uint8_t salt[16],
                        uint8_t cek[32]);

}  // namespace crypto
}  // namespace ebbackup
