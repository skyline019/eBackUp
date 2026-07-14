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
constexpr uint32_t kBackupFeatureGtCdc = 0x1000;
constexpr uint32_t kBackupFeatureGtCdcGear = 0x2000;
constexpr uint32_t kBackupFeatureGtCdcNative = 0x4000;
constexpr uint32_t kBackupFeatureGtCdcAnGear = 0x8000;
constexpr uint32_t kBackupFeatureGtCdcTwoFGear = 0x10000;
constexpr uint32_t kBackupFeatureTopoCdc = 0x20000;
constexpr uint32_t kBackupFeatureTopoChain = 0x40000;
constexpr uint32_t kBackupFeatureTopoPh = 0x80000;
constexpr uint32_t kBackupFeatureTopoPhNative = 0x100000;

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

inline bool RepoUsesGtCdc(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureGtCdc) != 0;
}

inline bool RepoUsesGtCdcGear(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureGtCdcGear) != 0;
}

inline bool RepoUsesGtCdcNative(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureGtCdcNative) != 0;
}

inline bool RepoUsesGtCdcAnGear(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureGtCdcAnGear) != 0;
}

inline bool RepoUsesGtCdcTwoFGear(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureGtCdcTwoFGear) != 0;
}

inline bool RepoUsesTopoCdc(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureTopoCdc) != 0;
}

inline bool RepoUsesTopoChain(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureTopoChain) != 0;
}

inline bool RepoUsesTopoPh(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureTopoPh) != 0;
}

inline bool RepoUsesTopoPhNative(const BackupSuperBlock& sb) {
  return (sb.ext.backup_features & kBackupFeatureTopoPhNative) != 0;
}

inline uint16_t RepoTopoPhnEventStride(const BackupSuperBlock& sb) {
  const uint16_t s = static_cast<uint16_t>(sb.ext.topo_reserved[0]) |
                     (static_cast<uint16_t>(sb.ext.topo_reserved[1]) << 8);
  return s == 0 ? 64 : s;
}

inline void SetRepoTopoPhnEventStride(BackupSuperBlock* sb, uint16_t stride) {
  sb->ext.topo_reserved[0] = static_cast<uint8_t>(stride & 0xFFu);
  sb->ext.topo_reserved[1] = static_cast<uint8_t>((stride >> 8) & 0xFFu);
}

// topo_reserved[2]: low 5 bits = k_points (0 → default 16); bit5 = persist δ enable.
constexpr uint8_t kTopoPhnKPointsMask = 0x1Fu;
constexpr uint8_t kTopoPhnPersistBit = 0x20u;

inline uint8_t RepoTopoPhnKPoints(const BackupSuperBlock& sb) {
  const uint8_t raw = static_cast<uint8_t>(sb.ext.topo_reserved[2] & kTopoPhnKPointsMask);
  return raw == 0 ? 16 : raw;
}

inline void SetRepoTopoPhnKPoints(BackupSuperBlock* sb, uint8_t k_points) {
  const uint8_t packed_k =
      static_cast<uint8_t>((k_points == 0 ? 16 : k_points) & kTopoPhnKPointsMask);
  const uint8_t persist =
      static_cast<uint8_t>(sb->ext.topo_reserved[2] & kTopoPhnPersistBit);
  sb->ext.topo_reserved[2] = static_cast<uint8_t>(packed_k | persist);
}

inline bool RepoTopoPhnPersistDelta(const BackupSuperBlock& sb) {
  return (sb.ext.topo_reserved[2] & kTopoPhnPersistBit) != 0;
}

inline void SetRepoTopoPhnPersistDelta(BackupSuperBlock* sb, bool enable) {
  if (enable) {
    sb->ext.topo_reserved[2] =
        static_cast<uint8_t>(sb->ext.topo_reserved[2] | kTopoPhnPersistBit);
  } else {
    sb->ext.topo_reserved[2] = static_cast<uint8_t>(sb->ext.topo_reserved[2] &
                                                    static_cast<uint8_t>(~kTopoPhnPersistBit));
  }
}

inline uint16_t RepoTopoPhCalibPermille(const BackupSuperBlock& sb) {
  return static_cast<uint16_t>(sb.ext.topo_reserved[0]) |
         (static_cast<uint16_t>(sb.ext.topo_reserved[1]) << 8);
}

inline void SetRepoTopoPhCalibPermille(BackupSuperBlock* sb, uint16_t permille) {
  sb->ext.topo_reserved[0] = static_cast<uint8_t>(permille & 0xFFu);
  sb->ext.topo_reserved[1] = static_cast<uint8_t>((permille >> 8) & 0xFFu);
}

inline uint8_t RepoTopoPhKPoints(const BackupSuperBlock& sb) {
  return sb.ext.topo_reserved[2] == 0 ? 16 : sb.ext.topo_reserved[2];
}

inline void SetRepoTopoPhKPoints(BackupSuperBlock* sb, uint8_t k_points) {
  sb->ext.topo_reserved[2] = k_points;
}

inline uint16_t RepoTopoCalibPermille(const BackupSuperBlock& sb) {
  return static_cast<uint16_t>(sb.ext.topo_reserved[0]) |
         (static_cast<uint16_t>(sb.ext.topo_reserved[1]) << 8);
}

inline void SetRepoTopoCalibPermille(BackupSuperBlock* sb, uint16_t permille) {
  sb->ext.topo_reserved[0] = static_cast<uint8_t>(permille & 0xFFu);
  sb->ext.topo_reserved[1] = static_cast<uint8_t>((permille >> 8) & 0xFFu);
}

inline uint16_t RepoTopoChainStrideLog(const BackupSuperBlock& sb) {
  return static_cast<uint16_t>(sb.ext.topo_reserved[0]) |
         (static_cast<uint16_t>(sb.ext.topo_reserved[1]) << 8);
}

inline void SetRepoTopoChainStrideLog(BackupSuperBlock* sb, uint16_t stride_log) {
  sb->ext.topo_reserved[0] = static_cast<uint8_t>(stride_log & 0xFFu);
  sb->ext.topo_reserved[1] = static_cast<uint8_t>((stride_log >> 8) & 0xFFu);
}

inline uint8_t RepoTopoChainQuantQ(const BackupSuperBlock& sb) {
  return sb.ext.topo_reserved[2];
}

inline void SetRepoTopoChainQuantQ(BackupSuperBlock* sb, uint8_t quant_q) {
  sb->ext.topo_reserved[2] = quant_q;
}

constexpr uint8_t kChainFeatureBeta1 = 0x01;

inline uint8_t RepoTopoChainFeatures(const BackupSuperBlock& sb) {
  return sb.ext.topo_reserved[3];
}

inline void SetRepoTopoChainFeatures(BackupSuperBlock* sb, uint8_t features) {
  sb->ext.topo_reserved[3] = features;
}

inline bool RepoTopoChainBeta1(const BackupSuperBlock& sb) {
  return (RepoTopoChainFeatures(sb) & kChainFeatureBeta1) != 0;
}

inline void SetRepoTopoChainBeta1(BackupSuperBlock* sb, bool enable) {
  uint8_t f = RepoTopoChainFeatures(*sb);
  if (enable) {
    f = static_cast<uint8_t>(f | kChainFeatureBeta1);
  } else {
    f = static_cast<uint8_t>(f & ~kChainFeatureBeta1);
  }
  SetRepoTopoChainFeatures(sb, f);
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

  bool sparse{false};
  std::vector<std::pair<uint64_t, uint64_t>> sparse_runs;
  std::vector<uint64_t> sparse_chunk_offsets;

  bool has_extended_meta() const {
    return file_type != FileType::kRegular || mode != 0 || mtime_unix != 0 ||
           atime_unix != 0 || uid != 0xFFFFFFFFu || gid != 0xFFFFFFFFu;
  }

  bool has_win_meta() const {
    return !security_descriptor_b64.empty() || inode_id != 0 ||
           reparse_tag != 0 || !reparse_target.empty() || !stream_name.empty();
  }

  bool has_sparse_meta() const {
    return sparse && !sparse_runs.empty();
  }

  bool efs_encrypted{false};
  std::string efs_key_blob_b64;

  bool has_efs_meta() const { return efs_encrypted; }

  bool needs_chunking() const {
    return file_type == FileType::kRegular && !efs_encrypted;
  }
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
