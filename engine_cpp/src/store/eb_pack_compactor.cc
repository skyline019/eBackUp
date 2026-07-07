#include "ebbackup/store/chunk_compactor.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <unordered_set>

#include "ebbackup/engine/manifest.h"
#include "ebbackup/common/fsync.h"
#include "ebbackup/store/chunk_index.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/eb_pack.h"
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

uint64_t SumPackBytes(const std::string& packs_dir) {
  uint64_t total = 0;
  if (!std::filesystem::exists(packs_dir)) return 0;
  for (const auto& ent : std::filesystem::directory_iterator(packs_dir)) {
    if (!ent.is_regular_file()) continue;
    if (ent.path().extension() != ".ebpack") continue;
    std::error_code ec;
    const auto sz = ent.file_size(ec);
    if (!ec) total += static_cast<uint64_t>(sz);
  }
  return total;
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

Status CompactEbPackStore(const std::string& repo_path,
                          const BackupSuperBlock& sb, bool dry_run,
                          CompactReport* report) {
  if (!report) return Status::InvalidArgument("report is null");

  const std::string chunks_path = RepoJoin(repo_path, "data/chunks");
  const std::string packs_dir =
      (std::filesystem::path(chunks_path).parent_path() / "packs").string();

  ChunkStore store(chunks_path);
  ConfigureStoreFromSuperBlock(&store, sb);
  const Status open_st = store.Open();
  if (!open_st.ok()) return open_st;

  report->physical_before = store.PhysicalBytes();

  std::unordered_set<std::string> referenced;
  const bool latest_only = !RepoUsesSnapshots(sb);
  const Status coll =
      CollectReferencedHashesForRepo(repo_path, latest_only, &referenced);
  if (!coll.ok()) return coll;

  uint64_t live_bytes = 0;
  uint64_t records = 0;
  const Status scan = store.ForEachRecord(
      [&](const uint8_t hash[32], uint64_t, uint32_t) {
        if (referenced.find(HashKey(hash)) != referenced.end()) {
          ++records;
        }
        return Status::Ok();
      });
  if (!scan.ok()) return scan;
  live_bytes = store.ComputeReferencedLiveBytes(referenced);

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

  const auto epoch = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::string temp_packs =
      packs_dir + ".compact_tmp." + std::to_string(static_cast<long long>(epoch));
  std::error_code ec;
  std::filesystem::remove_all(temp_packs, ec);
  std::filesystem::create_directories(temp_packs, ec);
  if (ec) return Status::IoError("cannot create compact temp packs dir");

  EbPackWriter writer(temp_packs, sb.critical.txn_id);
  std::vector<ChunkIndexEntry> new_index;
  new_index.reserve(static_cast<size_t>(records));

  const Status copy_st = store.ForEachRecord(
      [&](const uint8_t hash[32], uint64_t, uint32_t) {
        const std::string key = HashKey(hash);
        if (referenced.find(key) == referenced.end()) return Status::Ok();

        ChunkStore::ParsedHeader parsed{};
        std::vector<uint8_t> payload;
        const Status rd = store.ReadRecordForHash(hash, &parsed, &payload);
        if (!rd.ok()) return rd;

        EbPackRecordRef ref{};
        const Status wr = writer.AppendRecord(
            hash, payload.data(), payload.size(), parsed.header.uncompressed_len,
            static_cast<ChunkCodec>(parsed.header.codec), &ref);
        if (!wr.ok()) return wr;

        ChunkIndexEntry entry{};
        std::memcpy(entry.hash, hash, 32);
        entry.offset = ref.offset;
        entry.stored_len = ref.stored_len;
        entry.uncompressed_len = ref.uncompressed_len;
        entry.codec = ref.codec;
        entry.storage_flags = kChunkStorageEbPack;
        const std::string name =
            std::filesystem::path(ref.pack_path).filename().string();
        std::strncpy(entry.pack_name, name.c_str(), sizeof(entry.pack_name) - 1);
        new_index.push_back(entry);
        return Status::Ok();
      });
  if (!copy_st.ok()) {
    std::filesystem::remove_all(temp_packs, ec);
    return copy_st;
  }

  const Status flush_st = writer.FlushOpenPack(true);
  if (!flush_st.ok()) {
    std::filesystem::remove_all(temp_packs, ec);
    return flush_st;
  }
  const Status fs_st = writer.FsyncAll();
  if (!fs_st.ok()) {
    std::filesystem::remove_all(temp_packs, ec);
    return fs_st;
  }

  const std::string idx_path = ChunkIndexFile::PathForStore(chunks_path);
  const std::string idx_temp = idx_path + ".new";
  ChunkIndexFile index_file;
  const Status idx_st = index_file.Save(idx_temp, 0, new_index);
  if (!idx_st.ok()) {
    std::filesystem::remove_all(temp_packs, ec);
    return idx_st;
  }

  if (std::filesystem::exists(packs_dir)) {
    for (const auto& ent : std::filesystem::directory_iterator(packs_dir)) {
      if (!ent.is_regular_file()) continue;
      if (ent.path().extension() != ".ebpack") continue;
      std::filesystem::remove(ent.path(), ec);
    }
  } else {
    std::filesystem::create_directories(packs_dir, ec);
  }

  for (const auto& ent : std::filesystem::directory_iterator(temp_packs)) {
    if (!ent.is_regular_file()) continue;
    const auto dest = std::filesystem::path(packs_dir) / ent.path().filename();
    std::filesystem::rename(ent.path(), dest, ec);
    if (ec) {
      std::filesystem::remove_all(temp_packs, ec);
      return Status::IoError("compact pack rename failed");
    }
  }
  std::filesystem::remove_all(temp_packs, ec);

  if (std::filesystem::exists(chunks_path)) {
    std::filesystem::resize_file(chunks_path, 0, ec);
  }

  std::filesystem::rename(idx_temp, idx_path, ec);
  if (ec) return Status::IoError("compact index rename failed");
  const Status idx_fs = FsyncPath(idx_path);
  if (!idx_fs.ok()) return idx_fs;

  report->physical_after = SumPackBytes(packs_dir);
  report->ampl_ratio_after =
      live_bytes > 0 ? static_cast<double>(report->physical_after) /
                           static_cast<double>(live_bytes)
                     : 1.0;
  return Status::Ok();
}

}  // namespace ebbackup
