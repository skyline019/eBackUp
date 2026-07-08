#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {

struct EbBundleOptions {
  bool encrypt_bundle{false};
  std::string password;
};

struct EbBundleDeltaOptions {
  uint64_t base_txn_id{0};
  uint64_t target_txn_id{0};
  bool encrypt_bundle{false};
  std::string password;
};

struct EbBundleDeltaStats {
  uint64_t base_txn_id{0};
  uint64_t target_txn_id{0};
  uint64_t delta_chunk_count{0};
  uint64_t delta_bytes{0};
  double reuse_ratio{0.0};
};

Status ExportRepoToBundle(const std::string& repo_path,
                          const std::string& bundle_path,
                          const EbBundleOptions& options = EbBundleOptions{});

Status ExportRepoDeltaToBundle(const std::string& repo_path,
                               const std::string& bundle_path,
                               const EbBundleDeltaOptions& options,
                               EbBundleDeltaStats* stats = nullptr);

Status ImportBundleToRepo(const std::string& bundle_path,
                          const std::string& repo_path);

Status ImportBundleDeltaToRepo(const std::string& base_repo_or_bundle,
                               const std::string& delta_bundle_path,
                               const std::string& out_repo_path);

Status ApplyDeltaBundleToRepo(const std::string& delta_bundle_path,
                              const std::string& repo_path);

}  // namespace ebbackup
