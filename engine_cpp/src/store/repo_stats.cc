#include "ebbackup/store/repo_stats.h"

#include <filesystem>
#include <unordered_set>

#include "ebbackup/engine/manifest.h"
#include "ebbackup/state/superblock.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/orphan_gc.h"
#include "ebbackup/store/snapshot_store.h"

namespace ebbackup {

namespace {

std::string HashKey(const uint8_t hash[32]) {
  return std::string(reinterpret_cast<const char*>(hash), 32);
}

std::string RepoJoin(const std::string& repo, const std::string& name) {
  return (std::filesystem::path(repo) / name).string();
}

}  // namespace

Status ComputeRepoStats(const std::string& repo_path, RepoStats* out) {
  if (!out) return Status::InvalidArgument("out is null");
  *out = RepoStats{};

  const std::string manifest_path = RepoJoin(repo_path, "manifest");
  const bool has_manifest = std::filesystem::exists(manifest_path);

  BackupSuperBlockStore sb_store(RepoJoin(repo_path, "superblock.bin"));
  BackupSuperBlock sb{};
  const Status sb_st = sb_store.Load(&sb);
  if (!sb_st.ok()) return sb_st;

  if (!has_manifest) {
    if (sb.critical.txn_id != 0) {
      return Status::IoError("cannot open manifest: " + manifest_path);
    }
    ChunkStore store(RepoJoin(repo_path, "data/chunks"));
    if (RepoUsesPersistentIndex(sb)) {
      store.SetUsePersistentIndex(true);
    }
    if (RepoUsesEbPack(sb)) {
      store.SetUseEbPack(true);
    }
    const Status open_st = store.Open();
    if (open_st.ok()) {
      out->physical_bytes = store.PhysicalBytes();
      out->unique_chunks = store.record_count();
      out->tombstoned_chunks = store.tombstone_count();
      if (out->physical_bytes > 0) {
        out->orphan_bytes = out->physical_bytes;
      }
    }
    out->ampl_ratio = 1.0;
    return Status::Ok();
  }

  out->manifest_bytes = std::filesystem::file_size(manifest_path);

  ManifestDocument doc;
  const Status rd = ReadManifestAuto(manifest_path, &doc);
  if (!rd.ok()) return rd;

  ChunkStore store(RepoJoin(repo_path, "data/chunks"));
  if (RepoUsesPersistentIndex(sb)) {
    store.SetUsePersistentIndex(true);
  }
  if (RepoUsesEbPack(sb)) {
    store.SetUseEbPack(true);
  }
  const Status open_st = store.Open();
  if (!open_st.ok()) return open_st;

  out->physical_bytes = store.PhysicalBytes();
  out->unique_chunks = store.record_count();
  out->tombstoned_chunks = store.tombstone_count();

  std::unordered_set<std::string> referenced;
  const bool latest_only = !RepoUsesSnapshots(sb);
  const Status coll =
      CollectReferencedHashesForRepo(repo_path, latest_only, &referenced);
  if (!coll.ok()) return coll;

  uint64_t live = store.ComputeReferencedLiveBytes(referenced);

  out->live_bytes = live;
  if (out->physical_bytes > live) {
    out->orphan_bytes = out->physical_bytes - live;
  }
  out->ampl_ratio =
      live > 0 ? static_cast<double>(out->physical_bytes) / static_cast<double>(live)
               : 1.0;
  return Status::Ok();
}

}  // namespace ebbackup
