#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"

namespace ebbackup {

constexpr uint32_t kBackupFeatureSnapshots = 0x80;

constexpr uint32_t kSnapshotIndexMagic = 0x50414E53u;  // SNAP
constexpr uint32_t kSnapshotIndexVersion = 1;
constexpr size_t kSnapshotPathWidth = 64;

inline bool RepoUsesSnapshots(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureSnapshots) != 0;
}

#pragma pack(push, 1)
struct SnapshotIndexHeader {
  uint32_t magic{kSnapshotIndexMagic};
  uint32_t version{kSnapshotIndexVersion};
  uint64_t entry_count{0};
  uint32_t header_crc32{0};
};

struct SnapshotIndexRecord {
  uint64_t txn_id{0};
  int64_t created_at_unix{0};
  uint32_t manifest_crc32{0};
  uint8_t merkle_root[32]{};
  uint32_t file_count{0};
  char manifest_rel_path[kSnapshotPathWidth]{};
};
#pragma pack(pop)

struct SnapshotEntry {
  uint64_t txn_id{0};
  int64_t created_at_unix{0};
  uint32_t manifest_crc32{0};
  uint8_t merkle_root[32]{};
  uint32_t file_count{0};
  std::string manifest_rel_path;
};

std::string SnapshotsDir(const std::string& repo_path);
std::string SnapshotIndexPath(const std::string& repo_path);
std::string SnapshotManifestRelPath(uint64_t txn_id);
std::string SnapshotManifestPath(const std::string& repo_path, uint64_t txn_id);

Status LoadSnapshotIndex(const std::string& repo_path,
                         std::vector<SnapshotEntry>* out);
Status SaveSnapshotIndex(const std::string& repo_path,
                         const std::vector<SnapshotEntry>& entries);

Status ArchiveSnapshot(const std::string& repo_path, uint64_t txn_id,
                       const std::string& committed_manifest_path,
                       int64_t created_at_unix, uint32_t manifest_crc32,
                       const uint8_t merkle_root[32], uint32_t file_count);

Status LoadSnapshotManifest(const std::string& repo_path, uint64_t txn_id,
                            ManifestDocument* out);

Status DeleteSnapshot(const std::string& repo_path, uint64_t txn_id);

Status ListSnapshots(const std::string& repo_path,
                     std::vector<SnapshotEntry>* out);

}  // namespace ebbackup
