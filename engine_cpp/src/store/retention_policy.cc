#include "ebbackup/store/retention_policy.h"

#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <map>
#include <set>

#include "ebbackup/catalog/snapshot_meta.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/state/superblock.h"

namespace ebbackup {

RetentionPolicy DefaultRetentionPolicy() {
  RetentionPolicy p{};
  p.retain_min = 3;
  p.tiers = {
      {3600, 24},
      {86400, 7},
      {604800, 4},
      {2592000, 6},
  };
  return p;
}

Status ParseRetentionTiers(const std::string& spec, RetentionPolicy* out) {
  if (!out) return Status::InvalidArgument("out is null");
  RetentionPolicy parsed = DefaultRetentionPolicy();
  parsed.tiers.clear();
  if (spec.empty()) {
    *out = DefaultRetentionPolicy();
    return Status::Ok();
  }

  size_t start = 0;
  while (start < spec.size()) {
    const size_t comma = spec.find(',', start);
    const std::string token =
        spec.substr(start, comma == std::string::npos ? std::string::npos
                                                      : comma - start);
    const size_t colon = token.find(':');
    if (colon == std::string::npos || colon == 0) {
      *out = DefaultRetentionPolicy();
      return Status::InvalidArgument("invalid retention_tiers token");
    }
    RetentionTier tier{};
    tier.bucket_seconds = std::strtoll(token.c_str(), nullptr, 10);
    tier.keep_count = std::atoi(token.c_str() + colon + 1);
    if (tier.bucket_seconds <= 0 || tier.keep_count <= 0) {
      *out = DefaultRetentionPolicy();
      return Status::InvalidArgument("invalid retention tier values");
    }
    parsed.tiers.push_back(tier);
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  if (parsed.tiers.empty()) {
    *out = DefaultRetentionPolicy();
    return Status::InvalidArgument("empty retention tiers");
  }
  *out = parsed;
  return Status::Ok();
}

void ComputeKeepSet(const std::vector<SnapshotEntry>& entries,
                    const RetentionPolicy& policy,
                    std::unordered_set<uint64_t>* keep) {
  if (!keep) return;
  keep->clear();
  if (entries.empty()) return;

  std::set<uint64_t> keep_set;
  keep_set.insert(entries.back().txn_id);

  for (const auto& tier : policy.tiers) {
    if (tier.bucket_seconds <= 0 || tier.keep_count <= 0) continue;
    std::map<int64_t, uint64_t> bucket_best;
    for (const auto& e : entries) {
      const int64_t bucket = e.created_at_unix / tier.bucket_seconds;
      const auto it = bucket_best.find(bucket);
      if (it == bucket_best.end() || e.txn_id > it->second) {
        bucket_best[bucket] = e.txn_id;
      }
    }
    std::vector<int64_t> buckets;
    buckets.reserve(bucket_best.size());
    for (const auto& kv : bucket_best) buckets.push_back(kv.first);
    std::sort(buckets.begin(), buckets.end(), std::greater<int64_t>());
    const size_t take =
        std::min(static_cast<size_t>(tier.keep_count), buckets.size());
    for (size_t i = 0; i < take; ++i) {
      keep_set.insert(bucket_best[buckets[i]]);
    }
  }

  if (policy.retain_min > 0) {
    std::vector<uint64_t> by_txn;
    by_txn.reserve(entries.size());
    for (const auto& e : entries) by_txn.push_back(e.txn_id);
    std::sort(by_txn.begin(), by_txn.end());
    const size_t take = std::min(static_cast<size_t>(policy.retain_min),
                                 by_txn.size());
    for (size_t i = by_txn.size() - take; i < by_txn.size(); ++i) {
      keep_set.insert(by_txn[i]);
    }
  }

  keep->insert(keep_set.begin(), keep_set.end());
}

bool IsKeptByPolicy(const std::vector<SnapshotEntry>& entries,
                    const RetentionPolicy& policy, uint64_t txn_id) {
  std::unordered_set<uint64_t> keep;
  ComputeKeepSet(entries, policy, &keep);
  return keep.count(txn_id) > 0;
}

Status PruneSnapshots(const std::string& repo_path,
                      const RetentionPolicy& policy, bool dry_run,
                      PruneReport* report, const PruneOptions& options) {
  if (!report) return Status::InvalidArgument("report is null");
  *report = PruneReport{};

  BackupSuperBlock sb{};
  BackupSuperBlockStore store(RepoJoinUtf8(repo_path, "superblock.bin"));
  const Status sb_st = store.Load(&sb);
  if (!sb_st.ok()) return sb_st;
  if (RepoUsesImmutable(sb) && !dry_run && !options.authorized) {
    return Status::Conflict("immutable repo: audit authorization required");
  }

  std::vector<SnapshotEntry> entries;
  const Status load_st = ListSnapshots(repo_path, &entries);
  if (!load_st.ok()) return load_st;
  if (entries.empty()) return Status::Ok();

  std::unordered_map<uint64_t, catalog::SnapshotMetaRecord> meta;
  (void)catalog::LoadSnapshotMetaMap(repo_path, &meta);
  const int64_t now = static_cast<int64_t>(std::time(nullptr));

  std::unordered_set<uint64_t> keep;
  ComputeKeepSet(entries, policy, &keep);
  for (const auto& kv : meta) {
    if (kv.second.immutable_until_unix > now) {
      keep.insert(kv.first);
    }
  }

  for (const auto& e : entries) {
    if (keep.count(e.txn_id) > 0) continue;
    report->pruned_txn_ids.push_back(e.txn_id);
    ++report->pruned_count;
    if (!dry_run) {
      const Status del = DeleteSnapshot(repo_path, e.txn_id);
      if (!del.ok()) return del;
    }
  }
  report->kept_count = entries.size() - report->pruned_txn_ids.size();
  return Status::Ok();
}

}  // namespace ebbackup
