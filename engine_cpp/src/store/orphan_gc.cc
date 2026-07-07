#include "ebbackup/store/orphan_gc.h"

#include <cstring>
#include <filesystem>

#include "ebbackup/common/digest.h"
#include "ebbackup/store/snapshot_store.h"

namespace ebbackup {

namespace {

std::string HashKey(const uint8_t hash[32]) {
  return std::string(reinterpret_cast<const char*>(hash), 32);
}

Status MergeManifestHashes(const ManifestDocument& doc,
                           std::unordered_set<std::string>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  for (const auto& file : doc.files) {
    for (const auto& hex : file.chunk_hashes_hex) {
      uint8_t hash[32];
      if (!HexToBytes(hex, hash, 32)) {
        return Status::Corrupt("invalid manifest chunk hash");
      }
      out->insert(HashKey(hash));
    }
  }
  return Status::Ok();
}

}  // namespace

Status CollectReferencedHashes(const ManifestDocument& doc,
                               std::unordered_set<std::string>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  return MergeManifestHashes(doc, out);
}

Status CollectReferencedHashesRetained(const std::string& repo_path,
                                       std::unordered_set<std::string>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();

  const std::string manifest_path =
      (std::filesystem::path(repo_path) / "manifest").string();
  if (std::filesystem::exists(manifest_path)) {
    ManifestDocument latest{};
    const Status latest_st = ReadManifestAuto(manifest_path, &latest);
    if (!latest_st.ok()) return latest_st;
    const Status merge_latest = MergeManifestHashes(latest, out);
    if (!merge_latest.ok()) return merge_latest;
  }

  std::vector<SnapshotEntry> snapshots;
  const Status list_st = ListSnapshots(repo_path, &snapshots);
  if (!list_st.ok()) return list_st;
  for (const auto& snap : snapshots) {
    ManifestDocument doc{};
    const Status st = LoadSnapshotManifest(repo_path, snap.txn_id, &doc);
    if (!st.ok()) return st;
    const Status merge_st = MergeManifestHashes(doc, out);
    if (!merge_st.ok()) return merge_st;
  }
  return Status::Ok();
}

Status CollectReferencedHashesForRepo(const std::string& repo_path,
                                      bool latest_manifest_only,
                                      std::unordered_set<std::string>* out) {
  if (latest_manifest_only) {
    ManifestDocument doc;
    const Status rd =
        ReadManifestAuto((std::filesystem::path(repo_path) / "manifest").string(),
                         &doc);
    if (!rd.ok()) return rd;
    return CollectReferencedHashes(doc, out);
  }
  return CollectReferencedHashesRetained(repo_path, out);
}

Status ScanOrphansReferenced(ChunkStore* store,
                             const std::unordered_set<std::string>& referenced,
                             OrphanGcReport* report) {
  if (!store || !report) return Status::InvalidArgument("null argument");
  report->referenced_count = referenced.size();
  report->orphan_count = 0;
  report->tombstoned_count = store->tombstone_count();

  return store->ForEachRecord([&](const uint8_t hash[32], uint64_t, uint32_t) {
    if (referenced.find(HashKey(hash)) == referenced.end()) {
      ++report->orphan_count;
    }
    return Status::Ok();
  });
}

Status ExecuteOrphanGcReferenced(
    ChunkStore* store, const std::unordered_set<std::string>& referenced,
    bool dry_run, OrphanGcReport* report) {
  if (!store || !report) return Status::InvalidArgument("null argument");
  const Status scan = ScanOrphansReferenced(store, referenced, report);
  if (!scan.ok()) return scan;
  if (dry_run) return Status::Ok();

  return store->ForEachRecord([&](const uint8_t hash[32], uint64_t, uint32_t) {
    if (referenced.find(HashKey(hash)) == referenced.end()) {
      const Status ts = store->TombstoneHash(hash);
      if (!ts.ok()) return ts;
      ++report->tombstoned_count;
    }
    return Status::Ok();
  });
}

Status ScanOrphans(ChunkStore* store, const ManifestDocument& doc,
                   OrphanGcReport* report) {
  std::unordered_set<std::string> referenced;
  const Status coll = CollectReferencedHashes(doc, &referenced);
  if (!coll.ok()) return coll;
  return ScanOrphansReferenced(store, referenced, report);
}

Status ExecuteOrphanGc(ChunkStore* store, const ManifestDocument& doc,
                       bool dry_run, OrphanGcReport* report) {
  std::unordered_set<std::string> referenced;
  const Status coll = CollectReferencedHashes(doc, &referenced);
  if (!coll.ok()) return coll;
  return ExecuteOrphanGcReferenced(store, referenced, dry_run, report);
}

}  // namespace ebbackup
