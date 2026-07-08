#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/audit/merkle_proof.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"

namespace ebbackup {
namespace catalog {

struct ManifestDiffEntry {
  std::string path;
  uint64_t size_a{0};
  uint64_t size_b{0};
  std::string content_hash_a;
  std::string content_hash_b;
  std::string subset_merkle_hex;
  std::vector<audit::MerkleProofStep> merkle_proof;
};

struct SnapshotDiffAddedEntry {
  std::string path;
  std::string subset_merkle_hex;
  std::vector<audit::MerkleProofStep> merkle_proof;
};

struct SnapshotDiffResult {
  uint64_t txn_a{0};
  uint64_t txn_b{0};
  std::vector<SnapshotDiffAddedEntry> added;
  std::vector<std::string> removed;
  std::vector<ManifestDiffEntry> modified;
  double chunk_reuse_ratio{0.0};
  std::string diff_merkle_root_hex;
  std::vector<audit::MerkleProofStep> diff_merkle_proof;
};

Status DiffManifestDocuments(const ManifestDocument& a, const ManifestDocument& b,
                             DigestAlgo algo, SnapshotDiffResult* out);

std::string SnapshotDiffToJson(const SnapshotDiffResult& diff);

}  // namespace catalog
}  // namespace ebbackup
