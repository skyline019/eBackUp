#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/audit/merkle_proof.h"
#include "ebbackup/report/backup_report.h"

namespace ebbackup {
namespace catalog {

struct RestoreAcceptanceProofEntry {
  std::string path;
  std::string leaf_hex;
  std::string subset_merkle_hex;
  std::vector<audit::MerkleProofStep> merkle_proof;
};

struct RestoreAcceptanceReport {
  uint64_t txn_id{0};
  std::string subset_merkle_root_hex;
  uint64_t file_count{0};
  std::vector<std::string> paths;
  int verify_ok{0};
  uint64_t total_bytes{0};
  std::vector<RestoreAcceptanceProofEntry> merkle_proofs;
  std::vector<report::BackupPathIssue> issues;
};

std::string RestoreAcceptanceReportToJson(const RestoreAcceptanceReport& report);

Status BuildRestoreAcceptanceReport(
    const std::vector<std::string>& restored_paths,
    const std::string& subset_merkle_hex, uint64_t txn_id, uint64_t total_bytes,
    bool verify_ok, RestoreAcceptanceReport* out);

Status BuildRestoreAcceptanceReportWithFiles(
    const std::vector<std::pair<std::string, ManifestFileEntry>>& restored_files,
    const std::string& subset_merkle_hex, uint64_t txn_id, uint64_t total_bytes,
    bool verify_ok, DigestAlgo algo, RestoreAcceptanceReport* out);

}  // namespace catalog
}  // namespace ebbackup
