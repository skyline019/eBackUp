#include "ebbackup/store/snapshot_store.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "ebbackup/common/crc32.h"
#include "ebbackup/common/fsync.h"

namespace ebbackup {

namespace {

uint32_t HeaderCrc(const SnapshotIndexHeader& hdr) {
  SnapshotIndexHeader tmp = hdr;
  tmp.header_crc32 = 0;
  return Crc32(&tmp, sizeof(tmp));
}

void SetRecordPath(SnapshotIndexRecord* rec, const std::string& path) {
  std::memset(rec->manifest_rel_path, 0, kSnapshotPathWidth);
  const size_t n = std::min(path.size(), kSnapshotPathWidth - 1);
  std::memcpy(rec->manifest_rel_path, path.data(), n);
}

SnapshotEntry FromRecord(const SnapshotIndexRecord& rec) {
  SnapshotEntry e{};
  e.txn_id = rec.txn_id;
  e.created_at_unix = rec.created_at_unix;
  e.manifest_crc32 = rec.manifest_crc32;
  std::memcpy(e.merkle_root, rec.merkle_root, 32);
  e.file_count = rec.file_count;
  e.manifest_rel_path =
      std::string(rec.manifest_rel_path,
                  strnlen(rec.manifest_rel_path, kSnapshotPathWidth));
  return e;
}

SnapshotIndexRecord ToRecord(const SnapshotEntry& e) {
  SnapshotIndexRecord rec{};
  rec.txn_id = e.txn_id;
  rec.created_at_unix = e.created_at_unix;
  rec.manifest_crc32 = e.manifest_crc32;
  std::memcpy(rec.merkle_root, e.merkle_root, 32);
  rec.file_count = e.file_count;
  SetRecordPath(&rec, e.manifest_rel_path);
  return rec;
}

}  // namespace

std::string SnapshotsDir(const std::string& repo_path) {
  return (std::filesystem::path(repo_path) / "snapshots").string();
}

std::string SnapshotIndexPath(const std::string& repo_path) {
  return (std::filesystem::path(SnapshotsDir(repo_path)) / "index").string();
}

std::string SnapshotManifestRelPath(uint64_t txn_id) {
  char buf[64];
  snprintf(buf, sizeof(buf), "snapshots/%020llu.manifest",
           static_cast<unsigned long long>(txn_id));
  return std::string(buf);
}

std::string SnapshotManifestPath(const std::string& repo_path, uint64_t txn_id) {
  return (std::filesystem::path(repo_path) /
          SnapshotManifestRelPath(txn_id))
      .string();
}

Status LoadSnapshotIndex(const std::string& repo_path,
                         std::vector<SnapshotEntry>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  const std::string path = SnapshotIndexPath(repo_path);
  if (!std::filesystem::exists(path)) return Status::Ok();

  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::IoError("cannot open snapshot index: " + path);

  SnapshotIndexHeader hdr{};
  in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  if (!in) return Status::Corrupt("snapshot index header short");
  if (hdr.magic != kSnapshotIndexMagic ||
      hdr.version != kSnapshotIndexVersion) {
    return Status::Corrupt("snapshot index bad magic/version");
  }
  const uint32_t stored_crc = hdr.header_crc32;
  if (HeaderCrc(hdr) != stored_crc) {
    return Status::Corrupt("snapshot index header crc mismatch");
  }

  out->reserve(static_cast<size_t>(hdr.entry_count));
  for (uint64_t i = 0; i < hdr.entry_count; ++i) {
    SnapshotIndexRecord rec{};
    in.read(reinterpret_cast<char*>(&rec), sizeof(rec));
    if (!in) return Status::Corrupt("snapshot index entry short");
    out->push_back(FromRecord(rec));
  }
  std::sort(out->begin(), out->end(),
            [](const SnapshotEntry& a, const SnapshotEntry& b) {
              return a.txn_id < b.txn_id;
            });
  return Status::Ok();
}

Status SaveSnapshotIndex(const std::string& repo_path,
                         const std::vector<SnapshotEntry>& entries) {
  std::error_code ec;
  std::filesystem::create_directories(SnapshotsDir(repo_path), ec);
  const std::string path = SnapshotIndexPath(repo_path);
  const std::string temp = path + ".new";

  std::ofstream out(temp, std::ios::binary | std::ios::trunc);
  if (!out) return Status::IoError("cannot write snapshot index: " + temp);

  SnapshotIndexHeader hdr{};
  hdr.entry_count = entries.size();
  hdr.header_crc32 = HeaderCrc(hdr);
  out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  for (const auto& e : entries) {
    const SnapshotIndexRecord rec = ToRecord(e);
    out.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
  }
  out.flush();
  if (!out) return Status::IoError("snapshot index write failed");
  out.close();

  const Status fs = FsyncPath(temp);
  if (!fs.ok()) return fs;
  std::filesystem::rename(temp, path, ec);
  if (ec) return Status::IoError("snapshot index rename failed");
  return FsyncPath(path);
}

Status ArchiveSnapshot(const std::string& repo_path, uint64_t txn_id,
                       const std::string& committed_manifest_path,
                       int64_t created_at_unix, uint32_t manifest_crc32,
                       const uint8_t merkle_root[32], uint32_t file_count) {
  if (!merkle_root) return Status::InvalidArgument("merkle_root is null");

  std::error_code ec;
  std::filesystem::create_directories(SnapshotsDir(repo_path), ec);

  const std::string dest = SnapshotManifestPath(repo_path, txn_id);
  const std::string temp = dest + ".new";
  std::filesystem::copy_file(committed_manifest_path, temp,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  if (ec) {
    return Status::IoError("snapshot manifest copy failed: " + ec.message());
  }
  const Status fs_copy = FsyncPath(temp);
  if (!fs_copy.ok()) return fs_copy;
  std::filesystem::rename(temp, dest, ec);
  if (ec) return Status::IoError("snapshot manifest rename failed");
  const Status fs_dest = FsyncPath(dest);
  if (!fs_dest.ok()) return fs_dest;

  std::vector<SnapshotEntry> entries;
  const Status load_st = LoadSnapshotIndex(repo_path, &entries);
  if (!load_st.ok()) return load_st;

  entries.erase(
      std::remove_if(entries.begin(), entries.end(),
                     [&](const SnapshotEntry& e) { return e.txn_id == txn_id; }),
      entries.end());

  SnapshotEntry entry{};
  entry.txn_id = txn_id;
  entry.created_at_unix = created_at_unix;
  entry.manifest_crc32 = manifest_crc32;
  std::memcpy(entry.merkle_root, merkle_root, 32);
  entry.file_count = file_count;
  entry.manifest_rel_path = SnapshotManifestRelPath(txn_id);
  entries.push_back(entry);
  std::sort(entries.begin(), entries.end(),
            [](const SnapshotEntry& a, const SnapshotEntry& b) {
              return a.txn_id < b.txn_id;
            });
  return SaveSnapshotIndex(repo_path, entries);
}

Status LoadSnapshotManifest(const std::string& repo_path, uint64_t txn_id,
                            ManifestDocument* out) {
  if (!out) return Status::InvalidArgument("out is null");
  const std::string path = SnapshotManifestPath(repo_path, txn_id);
  if (!std::filesystem::exists(path)) {
    return Status::NotFound("snapshot manifest not found for txn");
  }
  return ReadManifestAuto(path, out);
}

Status DeleteSnapshot(const std::string& repo_path, uint64_t txn_id) {
  std::vector<SnapshotEntry> entries;
  const Status load_st = LoadSnapshotIndex(repo_path, &entries);
  if (!load_st.ok()) return load_st;

  const auto it =
      std::find_if(entries.begin(), entries.end(),
                   [&](const SnapshotEntry& e) { return e.txn_id == txn_id; });
  if (it == entries.end()) {
    return Status::NotFound("snapshot not in index");
  }

  const std::string path =
      (std::filesystem::path(repo_path) / it->manifest_rel_path).string();
  std::error_code ec;
  std::filesystem::remove(path, ec);
  entries.erase(it);
  return SaveSnapshotIndex(repo_path, entries);
}

Status ListSnapshots(const std::string& repo_path,
                     std::vector<SnapshotEntry>* out) {
  return LoadSnapshotIndex(repo_path, out);
}

}  // namespace ebbackup
