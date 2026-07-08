#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/digest.h"
#include "ebbackup/common/status.h"

namespace ebbackup {
namespace crypto {

constexpr size_t kMasterKeySize = 32;

struct CryptoEnvelope {
  uint32_t version{1};
  uint8_t salt[16]{};
  std::vector<uint8_t> wrap_password;
  std::vector<uint8_t> wrap_recovery;
};

std::string EnvelopePath(const std::string& repo_path);
bool EnvelopeExists(const std::string& repo_path);

Status LoadEnvelope(const std::string& repo_path, CryptoEnvelope* out);
Status SaveEnvelope(const std::string& repo_path, const CryptoEnvelope& env);

std::string GenerateRecoveryKey();
Status DeriveWrapKey(const std::string& secret, const uint8_t salt[16],
                     uint8_t wrap_key[32], DigestAlgo algo);

Status CreateEnvelope(const std::string& repo_path, const std::string& password,
                      std::string* recovery_key_out, uint8_t master_key[32]);
Status UpgradeLegacyToEnvelope(const std::string& repo_path,
                               const std::string& password,
                               std::string* recovery_key_out);

Status UnwrapMasterKeyWithPassword(const std::string& repo_path,
                                   const std::string& password,
                                   uint8_t master_key[32], DigestAlgo algo);
Status UnwrapMasterKeyWithRecoveryKey(const std::string& repo_path,
                                      const std::string& recovery_key,
                                      uint8_t master_key[32], DigestAlgo algo);

Status RotateEnvelopePassword(const std::string& repo_path,
                              const std::string& old_password,
                              const std::string& new_password,
                              DigestAlgo algo);

}  // namespace crypto
}  // namespace ebbackup
