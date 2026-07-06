#include "ebbackup/state/superblock.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace ebbackup {

namespace {

void ZeroPadding(BackupSuperBlock* b) {
  std::memset(b->critical.padding, 0, sizeof(b->critical.padding));
  std::memset(b->ext.ext_padding, 0, sizeof(b->ext.ext_padding));
  std::memset(b->critical.reserved_phase, 0, sizeof(b->critical.reserved_phase));
}

}  // namespace

BackupSuperBlockStore::BackupSuperBlockStore(std::string path)
    : path_(std::move(path)) {}

bool BackupSuperBlockStore::ValidateCritical(
    const BackupSuperBlockCritical& c) const {
  if (c.magic != kBackupMagic) return false;
  BackupSuperBlockCritical tmp = c;
  tmp.crc32 = 0;
  return Crc32(&tmp, kBackupSuperBlockCriticalSize) == c.crc32;
}

void BackupSuperBlockStore::MigrateLoadedBlock(BackupSuperBlock* block) {
  if (block->format_version >= kBackupSuperBlockFormatV2) return;
  block->ext.next_txn_id =
      std::max(block->critical.txn_id, block->critical.epoch) + 1;
  block->format_version = kBackupSuperBlockFormatV2;
}

void BackupSuperBlockStore::FinalizeCritical(BackupSuperBlock* block) {
  ZeroPadding(block);
  block->format_version = kBackupSuperBlockFormatV2;
  block->critical.crc32 = 0;
  block->critical.crc32 =
      Crc32(&block->critical, kBackupSuperBlockCriticalSize);
  block->block_crc32 =
      Crc32(block, kBackupSuperBlockSize - sizeof(block->block_crc32));
}

Status BackupSuperBlockStore::ReadSlot(int slot, BackupSuperBlock* out) {
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    return Status::IoError("cannot open superblock: " + path_);
  }
  in.seekg(static_cast<std::streamoff>(slot) * kBackupSuperBlockSize);
  in.read(reinterpret_cast<char*>(out), kBackupSuperBlockSize);
  if (!in) {
    return Status::IoError("superblock read short");
  }
  const uint64_t stored = out->block_crc32;
  out->block_crc32 = 0;
  if (Crc32(out, kBackupSuperBlockSize - sizeof(out->block_crc32)) != stored) {
    return Status::Corrupt("superblock outer crc32 mismatch");
  }
  out->block_crc32 = stored;
  if (!ValidateCritical(out->critical)) {
    return Status::Corrupt("superblock critical crc32 mismatch");
  }
  MigrateLoadedBlock(out);
  return Status::Ok();
}

Status BackupSuperBlockStore::WriteSlot(int slot, const BackupSuperBlock& block) {
  std::filesystem::create_directories(
      std::filesystem::path(path_).parent_path());
  std::fstream io(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!io) {
    std::ofstream create(path_, std::ios::binary);
    if (!create) {
      return Status::IoError("cannot create superblock: " + path_);
    }
    BackupSuperBlock zero{};
    create.write(reinterpret_cast<const char*>(&zero), kBackupSuperBlockSize);
    create.write(reinterpret_cast<const char*>(&zero), kBackupSuperBlockSize);
    create.close();
    io.open(path_, std::ios::binary | std::ios::in | std::ios::out);
  }
  io.seekp(static_cast<std::streamoff>(slot) * kBackupSuperBlockSize);
  io.write(reinterpret_cast<const char*>(&block), kBackupSuperBlockSize);
  io.flush();
  if (!io) {
    return Status::IoError("superblock write failed");
  }
  io.close();
  return FsyncPath(path_);
}

Status BackupSuperBlockStore::Load(BackupSuperBlock* out) {
  if (!std::filesystem::exists(path_)) {
    *out = BackupSuperBlock{};
    return Status::Ok();
  }
  BackupSuperBlock a{};
  BackupSuperBlock b{};
  const Status sa = ReadSlot(0, &a);
  const Status sb = ReadSlot(1, &b);
  const bool ok_a = sa.ok();
  const bool ok_b = sb.ok();
  if (!ok_a && !ok_b) {
    if (sa.code() == StatusCode::kIoError) return sa;
    return Status::Corrupt("CorruptSuperBlock");
  }
  if (ok_a && (!ok_b || a.critical.epoch >= b.critical.epoch)) {
    *out = a;
    return Status::Ok();
  }
  if (ok_b) {
    *out = b;
    return Status::Ok();
  }
  return Status::Corrupt("CorruptSuperBlock");
}

Status BackupSuperBlockStore::Commit(const BackupSuperBlock& in) {
  BackupSuperBlock current{};
  (void)Load(&current);
  BackupSuperBlock next = in;
  next.critical.epoch =
      std::max(current.critical.epoch, in.critical.epoch) + 1;
  FinalizeCritical(&next);
  const int inactive = (current.critical.epoch % 2 == 0) ? 1 : 0;
  return WriteSlot(inactive, next);
}

Status BackupSuperBlockStore::CorruptSlotForTest(int slot) {
  BackupSuperBlock block{};
  (void)Load(&block);
  block.critical.epoch = 0xDEADBEEFULL;
  block.critical.crc32 = 0;
  block.block_crc32 = 0;
  return WriteSlot(slot, block);
}

Status BackupSuperBlockStore::CorruptOuterCrcForTest(int slot) {
  std::ifstream in(path_, std::ios::binary);
  if (!in) return Status::IoError("cannot open superblock");
  BackupSuperBlock block{};
  in.seekg(static_cast<std::streamoff>(slot) * kBackupSuperBlockSize);
  in.read(reinterpret_cast<char*>(&block), kBackupSuperBlockSize);
  if (!in) return Status::IoError("superblock read short");
  block.block_crc32 ^= 0xFFFFFFFFu;
  std::fstream io(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!io) return Status::IoError("cannot open superblock for corrupt");
  io.seekp(static_cast<std::streamoff>(slot) * kBackupSuperBlockSize);
  io.write(reinterpret_cast<const char*>(&block), kBackupSuperBlockSize);
  io.flush();
  return Status::Ok();
}

}  // namespace ebbackup
