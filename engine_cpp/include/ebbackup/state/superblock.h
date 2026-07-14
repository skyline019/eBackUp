#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

#include "ebbackup/common/crc32.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/common/status.h"
#include "ebbackup/state/backup_phase.h"

namespace ebbackup {

constexpr uint64_t kBackupMagic = 0xEBBA0001ULL;
constexpr uint32_t kBackupSuperBlockFormatV1 = 1;
constexpr uint32_t kBackupSuperBlockFormatV2 = 2;
constexpr size_t kBackupSuperBlockSize = 4096;
constexpr size_t kBackupSuperBlockCriticalSize = 512;

#pragma pack(push, 1)
struct BackupSuperBlockCritical {
  uint64_t magic{kBackupMagic};
  uint64_t epoch{0};
  uint8_t phase{static_cast<uint8_t>(BackupPhase::kIdle)};
  uint8_t reserved_phase[7]{};
  uint64_t txn_id{0};
  uint64_t manifest_offset{0};
  uint64_t chunks_written{0};
  uint64_t bytes_processed{0};
  uint32_t crc32{0};
  uint8_t padding[452]{};
};

struct BackupSuperBlockExtension {
  uint64_t next_txn_id{1};
  uint32_t last_manifest_crc32{0};
  uint8_t merkle_root[32]{};
  uint8_t default_codec{0};
  uint32_t backup_features{0};
  uint32_t gtcdc_table_seed{0};
  uint8_t gtcdc_nc_level{0};
  uint8_t gtcdc_reserved[3]{};
  uint32_t topo_table_seed{0};
  uint8_t topo_variant{0};
  uint8_t topo_reserved[3]{};
  uint8_t ext_padding[3507]{};
};
#pragma pack(pop)

struct BackupSuperBlock {
  BackupSuperBlockCritical critical{};
  uint32_t format_version{kBackupSuperBlockFormatV2};
  BackupSuperBlockExtension ext{};
  uint64_t block_crc32{0};
};

static_assert(sizeof(BackupSuperBlockCritical) == 512,
              "BackupSuperBlockCritical must be 512 bytes");
static_assert(sizeof(BackupSuperBlockExtension) == 3572,
              "BackupSuperBlockExtension must be 3572 bytes");
static_assert(sizeof(BackupSuperBlock) == 4096,
              "BackupSuperBlock must be 4096 bytes");

class BackupSuperBlockStore {
 public:
  explicit BackupSuperBlockStore(std::string path);

  Status Load(BackupSuperBlock* out);
  Status Commit(const BackupSuperBlock& block);
  Status CorruptSlotForTest(int slot);
  Status CorruptOuterCrcForTest(int slot);

  const std::string& path() const { return path_; }

 private:
  Status ReadSlot(int slot, BackupSuperBlock* out);
  Status WriteSlot(int slot, const BackupSuperBlock& block);
  void FinalizeCritical(BackupSuperBlock* block);
  bool ValidateCritical(const BackupSuperBlockCritical& c) const;
  void MigrateLoadedBlock(BackupSuperBlock* block);

  std::string path_;
};

inline BackupPhase GetPhase(const BackupSuperBlock& sb) {
  return static_cast<BackupPhase>(sb.critical.phase);
}

inline void SetPhase(BackupSuperBlock* sb, BackupPhase phase) {
  sb->critical.phase = static_cast<uint8_t>(phase);
}

inline uint64_t GetNextTxnId(const BackupSuperBlock& sb) {
  if (sb.format_version >= kBackupSuperBlockFormatV2) {
    return sb.ext.next_txn_id;
  }
  return std::max(sb.critical.txn_id, sb.critical.epoch) + 1;
}

inline void SetNextTxnId(BackupSuperBlock* sb, uint64_t id) {
  sb->ext.next_txn_id = id;
  sb->format_version = kBackupSuperBlockFormatV2;
}

}  // namespace ebbackup
