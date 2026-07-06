#include "ebbackup/store/orphan_gc.h"

#include <cstring>

#include "ebbackup/common/digest.h"

namespace ebbackup {

namespace {

std::string HashKey(const uint8_t hash[32]) {
  return std::string(reinterpret_cast<const char*>(hash), 32);
}

}  // namespace

Status CollectReferencedHashes(const ManifestDocument& doc,
                               std::unordered_set<std::string>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
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

Status ScanOrphans(ChunkStore* store, const ManifestDocument& doc,
                   OrphanGcReport* report) {
  if (!store || !report) return Status::InvalidArgument("null argument");
  report->referenced_count = 0;
  report->orphan_count = 0;
  report->tombstoned_count = store->tombstone_count();

  std::unordered_set<std::string> referenced;
  const Status coll = CollectReferencedHashes(doc, &referenced);
  if (!coll.ok()) return coll;
  report->referenced_count = referenced.size();

  return store->ForEachRecord([&](const uint8_t hash[32], uint64_t, uint32_t) {
    if (referenced.find(HashKey(hash)) == referenced.end()) {
      ++report->orphan_count;
    }
    return Status::Ok();
  });
}

Status ExecuteOrphanGc(ChunkStore* store, const ManifestDocument& doc,
                       bool dry_run, OrphanGcReport* report) {
  if (!store || !report) return Status::InvalidArgument("null argument");
  const Status scan = ScanOrphans(store, doc, report);
  if (!scan.ok()) return scan;
  if (dry_run) return Status::Ok();

  std::unordered_set<std::string> referenced;
  const Status coll = CollectReferencedHashes(doc, &referenced);
  if (!coll.ok()) return coll;

  return store->ForEachRecord([&](const uint8_t hash[32], uint64_t, uint32_t) {
    if (referenced.find(HashKey(hash)) == referenced.end()) {
      const Status ts = store->TombstoneHash(hash);
      if (!ts.ok()) return ts;
      ++report->tombstoned_count;
    }
    return Status::Ok();
  });
}

}  // namespace ebbackup
