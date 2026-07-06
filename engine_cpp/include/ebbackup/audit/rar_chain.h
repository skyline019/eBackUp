#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {
namespace audit {

struct RarChainEntry {
  uint64_t sequence{0};
  uint64_t txn_id{0};
  std::string manifest_crc32;
  std::string merkle_root;
  std::string rar_sha256;
  std::string prev_rar_sha256;
  std::string body_json;
  int64_t generated_at_unix{0};
  std::string signature;
};

struct RarChainVerifyReport {
  uint64_t entry_count{0};
  uint64_t last_sequence{0};
  bool consistent{true};
  std::string last_rar_sha256;
  std::vector<std::string> errors;
};

Status AppendRarChainEntry(const std::string& chain_path,
                           const RarChainEntry& entry);

Status ReadRarChainEntries(const std::string& chain_path,
                           std::vector<RarChainEntry>* out);

Status ReadLastRarChainEntry(const std::string& chain_path, RarChainEntry* out,
                             bool* found);

Status VerifyRarChain(const std::string& chain_path, RarChainVerifyReport* out);

std::string RarChainLastSha256(const std::string& chain_path);

uint64_t RarChainNextSequence(const std::string& chain_path);

std::string BuildRarBodyJson(uint64_t txn_id, uint32_t manifest_crc32,
                             const uint8_t merkle_root[32]);

std::string ComputeRarSha256(const std::string& body_json);

}  // namespace audit
}  // namespace ebbackup
