#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ebbackup/chunk/cfi_index.h"
#include "ebbackup/common/status.h"
#include "ebbackup/state/superblock.h"

namespace ebbackup {

enum class FileType : uint8_t {
  kRegular = 0,
  kDirectory = 1,
  kSymlink = 2,
  kFifo = 3,
  kBlock = 4,
  kChar = 5,
};

constexpr uint32_t kBackupFeatureMeta = 0x01;
constexpr uint32_t kBackupFeatureSpecialFiles = 0x02;
constexpr uint32_t kBackupFeatureEncrypted = 0x04;
constexpr uint32_t kBackupFeatureDigestStandard = 0x08;
constexpr uint32_t kBackupFeaturePersistentIndex = 0x10;
constexpr uint32_t kBackupFeatureManifestBinary = 0x20;
constexpr uint32_t kBackupFeatureBalancedDurability = 0x40;

constexpr uint32_t kBackupFeatureCoalescedMeta = 0x200;
constexpr uint32_t kBackupFeatureEbPack = 0x100;
constexpr uint32_t kBackupFeatureWinMeta = 0x400;
constexpr uint32_t kBackupFeatureImmutable = 0x800;

inline bool RepoUsesCoalescedMeta(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureCoalescedMeta) != 0;
}

inline bool RepoUsesEbPack(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureEbPack) != 0;
}

constexpr uint8_t kDefaultCodecOff = 0;
constexpr uint8_t kDefaultCodecLz4 = 1;
constexpr uint8_t kDefaultCodecZstd = 2;
constexpr uint8_t kDefaultCodecAuto = 3;

inline bool RepoUsesPersistentIndex(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeaturePersistentIndex) != 0;
}

inline bool RepoUsesManifestBinary(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureManifestBinary) != 0;
}

inline bool RepoUsesBalancedDurability(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureBalancedDurability) != 0;
}

inline bool RepoUsesImmutable(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureImmutable) != 0;
}

struct ManifestFileEntry {
  std::string relative_path;
  uint64_t size{0};
  std::vector<std::string> chunk_hashes_hex;
  uint32_t browse_chunk_count{0};
  CfiIndex cfi;

  FileType file_type{FileType::kRegular};
  uint32_t mode{0};
  uint32_t uid{0xFFFFFFFFu};
  uint32_t gid{0xFFFFFFFFu};
  int64_t mtime_unix{0};
  int64_t atime_unix{0};
  std::string symlink_target;
  uint32_t device_major{0};
  uint32_t device_minor{0};

  std::string security_descriptor_b64;
  uint64_t inode_id{0};
  uint32_t reparse_tag{0};
  std::string reparse_target;
  std::string stream_name;

  bool has_extended_meta() const {
    return file_type != FileType::kRegular || mode != 0 || mtime_unix != 0 ||
           atime_unix != 0 || uid != 0xFFFFFFFFu || gid != 0xFFFFFFFFu;
  }

  bool has_win_meta() const {
    return !security_descriptor_b64.empty() || inode_id != 0 ||
           reparse_tag != 0 || !reparse_target.empty() || !stream_name.empty();
  }

  bool needs_chunking() const { return file_type == FileType::kRegular; }
};

struct ManifestDocument {
  uint64_t txn_id{0};
  std::vector<ManifestFileEntry> files;
};

const char* FileTypeToString(FileType type);
FileType FileTypeFromString(const std::string& s);

Status WriteManifestV2(const std::string& path, const ManifestDocument& doc);
Status WriteManifestV3(const std::string& path, const ManifestDocument& doc);
Status WriteManifestV4(const std::string& path, const ManifestDocument& doc);
Status WriteManifestV5(const std::string& path, const ManifestDocument& doc);
Status WriteManifestAuto(const std::string& path, const ManifestDocument& doc,
                         bool prefer_binary = false);
Status ReadManifestAuto(const std::string& path, ManifestDocument* out);
Status ComputeManifestBodyCrc32(const std::string& body, uint32_t* out);
Status ComputeManifestV4BodyCrc32(const std::vector<uint8_t>& body, uint32_t* out);

ManifestFileEntry ManifestEntryFromScanMeta(const std::string& relative_path,
                                            FileType type, uint64_t size,
                                            uint32_t mode, uint32_t uid,
                                            uint32_t gid, int64_t mtime,
                                            int64_t atime,
                                            const std::string& symlink_target,
                                            uint32_t dev_major,
                                            uint32_t dev_minor);

struct ManifestBrowseEntry {
  std::string relative_path;
  uint64_t size{0};
  FileType file_type{FileType::kRegular};
  int64_t mtime_unix{0};
  uint32_t chunk_count{0};
};

using ManifestBrowseEntryFn =
    std::function<Status(const ManifestBrowseEntry& entry)>;

Status IterateManifestBinaryEntries(const std::string& path,
                                    const ManifestBrowseEntryFn& fn,
                                    uint64_t* txn_id_out = nullptr);

Status BuildManifestBrowseIndexFromFile(const std::string& manifest_path,
                                        uint64_t txn_id,
                                        const std::string& out_path);

}  // namespace ebbackup
