#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {
namespace crypto {

Status Aes256GcmEncryptPortable(const uint8_t key[32], const uint8_t* plain,
                                size_t plain_len, std::vector<uint8_t>* out);

Status Aes256GcmDecryptPortable(const uint8_t key[32], const uint8_t* blob,
                                size_t blob_len, std::vector<uint8_t>* out);

Status Aes256GcmEncryptPortableEx(const uint8_t key[32], const uint8_t nonce[12],
                                  const uint8_t* aad, size_t aad_len,
                                  const uint8_t* plain, size_t plain_len,
                                  std::vector<uint8_t>* out);

Status Aes256GcmDecryptPortableEx(const uint8_t key[32], const uint8_t* blob,
                                  size_t blob_len, const uint8_t* aad,
                                  size_t aad_len, std::vector<uint8_t>* out);

}  // namespace crypto
}  // namespace ebbackup
