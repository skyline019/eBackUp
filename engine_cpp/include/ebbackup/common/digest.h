#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "ebbackup/engine/manifest.h"
#include "ebbackup/state/superblock.h"

namespace ebbackup {

enum class DigestAlgo { kLegacy = 0, kStandard = 1 };

void ContentHash(DigestAlgo algo, const uint8_t* data, size_t len,
                 uint8_t hash_out[32]);
void HmacSha256(DigestAlgo algo, const uint8_t* key, size_t key_len,
                const uint8_t* data, size_t data_len, uint8_t out[32]);
void Pbkdf2Sha256(DigestAlgo algo, const uint8_t* password, size_t password_len,
                  const uint8_t* salt, size_t salt_len, uint32_t iterations,
                  uint8_t out[32]);

// Routes to kLegacy; prefer ContentHash(digest_algo, ...) in engine paths.
void Sha256(const uint8_t* data, size_t len, uint8_t hash_out[32]);
std::string Sha256Hex(const uint8_t* data, size_t len);
std::string Sha256HexString(const std::string& data);
std::string BytesToHex(const uint8_t* data, size_t len);
bool HexToBytes(const std::string& hex, uint8_t* out, size_t out_len);

inline bool RepoUsesStandardDigest(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureDigestStandard) != 0;
}

inline DigestAlgo DigestAlgoFromSuperBlock(const BackupSuperBlock& sb) {
  return RepoUsesStandardDigest(sb) ? DigestAlgo::kStandard
                                    : DigestAlgo::kLegacy;
}

}  // namespace ebbackup
