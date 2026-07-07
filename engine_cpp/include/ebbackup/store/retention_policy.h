#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/store/snapshot_store.h"

namespace ebbackup {

struct RetentionTier {
  int64_t bucket_seconds{0};
  int keep_count{0};
};

struct RetentionPolicy {
  std::vector<RetentionTier> tiers;
  int retain_min{3};
};

struct PruneReport {
  uint64_t kept_count{0};
  uint64_t pruned_count{0};
  std::vector<uint64_t> pruned_txn_ids;
};

RetentionPolicy DefaultRetentionPolicy();

Status ParseRetentionTiers(const std::string& spec, RetentionPolicy* out);

void ComputeKeepSet(const std::vector<SnapshotEntry>& entries,
                    const RetentionPolicy& policy,
                    std::unordered_set<uint64_t>* keep);

Status PruneSnapshots(const std::string& repo_path,
                      const RetentionPolicy& policy, bool dry_run,
                      PruneReport* report);

bool IsKeptByPolicy(const std::vector<SnapshotEntry>& entries,
                    const RetentionPolicy& policy, uint64_t txn_id);

}  // namespace ebbackup
