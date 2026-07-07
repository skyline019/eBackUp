#include "ebbackup/store/chunk_compactor.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unordered_set>

#include "ebbackup/common/crc32.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/state/backup_phase.h"
#include "ebbackup/state/superblock.h"
#include "ebbackup/store/chunk_index.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/orphan_gc.h"
#include "ebbackup/store/snapshot_store.h"

namespace ebbackup {

namespace {

std::string RepoJoin(const std::string& repo, const std::string& name) {
  return (std::filesystem::path(repo) / name).string();
}

std::string HashKey(const uint8_t hash[32]) {
  return std::string(reinterpret_cast<const char*>(hash), 32);
}

uint32_t HeaderCrcV2(const ChunkRecordHeader& hdr) {
  ChunkRecordHeader tmp = hdr;
  tmp.record_crc32 = 0;
  return Crc32(&tmp, sizeof(tmp));
}

void ConfigureStoreFromSuperBlock(ChunkStore* store, const BackupSuperBlock& sb) {
  if (RepoUsesPersistentIndex(sb)) {
    store->SetUsePersistentIndex(true);
  }
  if (RepoUsesEbPack(sb)) {
    store->SetUseEbPack(true);
  }
  if (RepoUsesBalancedDurability(sb)) {
    store->SetDurabilityMode(DurabilityMode::kBalanced);
  }
}

}  // namespace

Status CompactChunkStore(const std::string& repo_path, bool dry_run,
                         CompactReport* report) {
  if (!report) return Status::InvalidArgument("report is null");

  BackupSuperBlockStore sb_store(RepoJoin(repo_path, "superblock.bin"));
  BackupSuperBlock sb{};
  const Status sb_st = sb_store.Load(&sb);
  if (!sb_st.ok()) return sb_st;
  if (GetPhase(sb) != BackupPhase::kIdle &&
      GetPhase(sb) != BackupPhase::kComplete) {
    return Status::Conflict("repo busy; compact requires idle phase");
  }

  ChunkStore store(RepoJoin(repo_path, "data/chunks"));
  ConfigureStoreFromSuperBlock(&store, sb);
  const Status open_st = store.Open();
  if (!open_st.ok()) return open_st;

  if (RepoUsesEbPack(sb)) {
    return CompactEbPackStore(repo_path, sb, dry_run, report);
  }

  report->physical_before = store.file_size();

  std::unordered_set<std::string> referenced;
  const bool latest_only = !RepoUsesSnapshots(sb);
  const Status coll =
      CollectReferencedHashesForRepo(repo_path, latest_only, &referenced);
  if (!coll.ok()) return coll;

  uint64_t live_bytes = 0;
  uint64_t records = 0;
  const Status scan = store.ForEachRecord(
      [&](const uint8_t hash[32], uint64_t, uint32_t stored_len) {
        if (referenced.find(HashKey(hash)) != referenced.end()) {
          live_bytes += kChunkHeaderV2Size + stored_len;
          ++records;
        }
        return Status::Ok();
      });
  if (!scan.ok()) return scan;

  report->live_bytes = live_bytes;
  report->records_copied = records;
  report->ampl_ratio_before =
      live_bytes > 0 ? static_cast<double>(report->physical_before) /
                           static_cast<double>(live_bytes)
                     : 1.0;

  if (dry_run) {
    report->physical_after = report->physical_before;
    report->ampl_ratio_after = report->ampl_ratio_before;
    return Status::Ok();
  }

  const std::string chunks_path = store.path();
  const std::string temp_path = chunks_path + ".pack.tmp";
  const auto epoch = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::string backup_path =
      chunks_path + ".bak." + std::to_string(static_cast<long long>(epoch));

  std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
  if (!out) return Status::IoError("cannot open compact temp");

  std::vector<ChunkIndexEntry> new_index;
  uint64_t offset = 0;

  const Status copy_st = store.ForEachRecord(
      [&](const uint8_t hash[32], uint64_t src_offset, uint32_t) {
        const std::string key = HashKey(hash);
        if (referenced.find(key) == referenced.end()) return Status::Ok();

        ChunkStore::ParsedHeader parsed{};
        std::vector<uint8_t> payload;
        const Status rd_rec = store.ReadRecordAt(src_offset, &parsed, &payload);
        if (!rd_rec.ok()) return rd_rec;

        ChunkRecordHeader hdr = parsed.header;
        hdr.record_crc32 = 0;
        hdr.record_crc32 = HeaderCrcV2(hdr);

        out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        out.write(reinterpret_cast<const char*>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
        if (!out) return Status::IoError("compact write failed");

        ChunkIndexEntry entry{};
        std::memcpy(entry.hash, hash, 32);
        entry.offset = offset;
        entry.stored_len = hdr.stored_len;
        entry.uncompressed_len = hdr.uncompressed_len;
        entry.codec = hdr.codec;
        new_index.push_back(entry);
        offset += sizeof(hdr) + hdr.stored_len;
        return Status::Ok();
      });
  if (!copy_st.ok()) return copy_st;

  out.flush();
  if (!out) return Status::IoError("compact flush failed");
  out.close();
  const Status fs = FsyncPath(temp_path);
  if (!fs.ok()) return fs;

  std::error_code ec;
  if (std::filesystem::exists(chunks_path)) {
    std::filesystem::rename(chunks_path, backup_path, ec);
    if (ec) return Status::IoError("compact backup rename failed");
  }
  std::filesystem::rename(temp_path, chunks_path, ec);
  if (ec) return Status::IoError("compact swap rename failed");
  const Status fs2 = FsyncPath(chunks_path);
  if (!fs2.ok()) return fs2;

  ChunkIndexFile index_file;
  const Status idx_st =
      index_file.Save(ChunkIndexFile::PathForStore(chunks_path), offset,
                      new_index);
  if (!idx_st.ok()) return idx_st;

  report->physical_after = offset;
  report->ampl_ratio_after =
      live_bytes > 0 ? static_cast<double>(report->physical_after) /
                           static_cast<double>(live_bytes)
                     : 1.0;
  return Status::Ok();
}

Status WaitForRepoIdle(const std::string& repo_path, int timeout_seconds) {
  if (timeout_seconds < 0) {
    return Status::InvalidArgument("timeout_seconds must be >= 0");
  }
  const std::string sb_path = RepoJoin(repo_path, "superblock.bin");
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(timeout_seconds);
  while (true) {
    BackupSuperBlockStore store(sb_path);
    BackupSuperBlock sb{};
    const Status st = store.Load(&sb);
    if (!st.ok()) return st;
    const BackupPhase phase = GetPhase(sb);
    if (phase == BackupPhase::kIdle || phase == BackupPhase::kComplete ||
        phase == BackupPhase::kAborted) {
      return Status::Ok();
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return Status::Conflict("repo busy; timed out waiting for idle phase");
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

}  // namespace ebbackup
