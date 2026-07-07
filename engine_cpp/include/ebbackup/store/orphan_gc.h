#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/store/chunk_store.h"

namespace ebbackup {

struct OrphanGcReport {
  uint64_t referenced_count{0};
  uint64_t orphan_count{0};
  uint64_t tombstoned_count{0};
};

Status CollectReferencedHashes(const ManifestDocument& doc,
                               std::unordered_set<std::string>* out);

Status CollectReferencedHashesRetained(const std::string& repo_path,
                                       std::unordered_set<std::string>* out);

Status CollectReferencedHashesForRepo(const std::string& repo_path,
                                      bool latest_manifest_only,
                                      std::unordered_set<std::string>* out);

Status ScanOrphansReferenced(ChunkStore* store,
                             const std::unordered_set<std::string>& referenced,
                             OrphanGcReport* report);

Status ExecuteOrphanGcReferenced(
    ChunkStore* store, const std::unordered_set<std::string>& referenced,
    bool dry_run, OrphanGcReport* report);

Status ScanOrphans(ChunkStore* store, const ManifestDocument& doc,
                   OrphanGcReport* report);

Status ExecuteOrphanGc(ChunkStore* store, const ManifestDocument& doc, bool dry_run,
                       OrphanGcReport* report);

}  // namespace ebbackup
