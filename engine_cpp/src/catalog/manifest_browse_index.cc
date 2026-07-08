#include "ebbackup/catalog/manifest_browse_index.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/store/snapshot_store.h"

namespace ebbackup {
namespace catalog {

namespace {

constexpr char kMbiMagic[8] = {'E', 'B', 'M', 'B', 'I', '0', '0', '1'};
constexpr uint32_t kMbiVersion = 1;
constexpr size_t kMbiHeaderSize = 24;

void WriteU32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 24));
}

void WriteU64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
}

void WriteI64(std::vector<uint8_t>& out, int64_t v) {
  WriteU64(out, static_cast<uint64_t>(v));
}

bool ReadU32(const uint8_t* p, uint32_t* out) {
  *out = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
  return true;
}

bool ReadU64(const uint8_t* p, uint64_t* out) {
  *out = 0;
  for (int i = 0; i < 8; ++i) {
    *out |= static_cast<uint64_t>(p[i]) << (8 * i);
  }
  return true;
}

bool ReadI64(const uint8_t* p, int64_t* out) {
  uint64_t tmp = 0;
  if (!ReadU64(p, &tmp)) return false;
  *out = static_cast<int64_t>(tmp);
  return true;
}

void SerializeRecord(const ManifestBrowseRecord& rec, std::vector<uint8_t>& out) {
  WriteU32(out, static_cast<uint32_t>(rec.relative_path.size()));
  out.insert(out.end(), rec.relative_path.begin(), rec.relative_path.end());
  WriteU64(out, rec.size);
  out.push_back(static_cast<uint8_t>(rec.file_type));
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  WriteI64(out, rec.mtime_unix);
  WriteU32(out, rec.chunk_count);
}

Status DeserializeRecord(const uint8_t* data, size_t data_len,
                         ManifestBrowseRecord* out, size_t* consumed) {
  if (!out || !consumed) return Status::InvalidArgument("out is null");
  if (data_len < 28) return Status::Corrupt("browse record truncated");
  uint32_t path_len = 0;
  ReadU32(data, &path_len);
  if (data_len < 4 + path_len + 24) {
    return Status::Corrupt("browse record truncated");
  }
  out->relative_path.assign(reinterpret_cast<const char*>(data + 4), path_len);
  const uint8_t* p = data + 4 + path_len;
  ReadU64(p, &out->size);
  p += 8;
  out->file_type = static_cast<FileType>(*p);
  p += 4;
  ReadI64(p, &out->mtime_unix);
  p += 8;
  ReadU32(p, &out->chunk_count);
  *consumed = static_cast<size_t>((p + 4) - data);
  return Status::Ok();
}

bool ReadPathAtOffset(std::ifstream& in, uint64_t offset, std::string* path) {
  in.seekg(static_cast<std::streamoff>(offset));
  uint32_t path_len = 0;
  in.read(reinterpret_cast<char*>(&path_len), 4);
  if (!in || path_len > 16 * 1024 * 1024) return false;
  path->resize(path_len);
  if (path_len > 0) {
    in.read(path->data(), static_cast<std::streamsize>(path_len));
  }
  return static_cast<bool>(in);
}

int ComparePathAtOffset(std::ifstream& in, uint64_t offset,
                        const std::string& prefix) {
  std::string path;
  if (!ReadPathAtOffset(in, offset, &path)) return 0;
  if (prefix.empty()) return -1;
  if (path.size() < prefix.size()) {
    const int cmp = path.compare(prefix.substr(0, path.size()));
    return cmp == 0 ? -1 : cmp;
  }
  return path.compare(0, prefix.size(), prefix);
}

}  // namespace

std::string ManifestBrowseIndexDir(const std::string& repo_path) {
  return RepoJoinUtf8(repo_path, "catalog/manifest_browse");
}

std::string ManifestBrowseIndexPath(const std::string& repo_path, uint64_t txn_id) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%llu.mbi",
                static_cast<unsigned long long>(txn_id));
  return RepoJoinUtf8(ManifestBrowseIndexDir(repo_path), buf);
}

Status WriteManifestBrowseIndexToPath(const std::string& index_path, uint64_t txn_id,
                                      std::vector<ManifestBrowseRecord> records) {
  std::sort(records.begin(), records.end(),
            [](const ManifestBrowseRecord& a, const ManifestBrowseRecord& b) {
              return a.relative_path < b.relative_path;
            });
  for (auto& rec : records) rec.txn_id = txn_id;

  std::vector<uint8_t> blob;
  std::vector<uint64_t> offsets;
  const uint64_t records_base =
      kMbiHeaderSize + static_cast<uint64_t>(records.size()) * 8;
  uint64_t pos = records_base;
  for (const auto& rec : records) {
    offsets.push_back(pos);
    const size_t before = blob.size();
    SerializeRecord(rec, blob);
    pos += static_cast<uint64_t>(blob.size() - before);
  }

  std::error_code ec;
  std::filesystem::create_directories(
      PathFromUtf8(PathToUtf8(PathFromUtf8(index_path).parent_path())), ec);
  const std::string temp = index_path + ".new";
  std::ofstream out(PathFromUtf8(temp), std::ios::binary | std::ios::trunc);
  if (!out) return Status::IoError("cannot write browse index: " + temp);

  out.write(kMbiMagic, 8);
  const uint32_t version = kMbiVersion;
  out.write(reinterpret_cast<const char*>(&version), 4);
  out.write(reinterpret_cast<const char*>(&txn_id), 8);
  const uint32_t count = static_cast<uint32_t>(records.size());
  out.write(reinterpret_cast<const char*>(&count), 4);
  for (uint64_t off : offsets) {
    out.write(reinterpret_cast<const char*>(&off), 8);
  }
  if (!blob.empty()) {
    out.write(reinterpret_cast<const char*>(blob.data()),
              static_cast<std::streamsize>(blob.size()));
  }
  out.close();
  if (!out) return Status::IoError("browse index write failed");
  std::filesystem::rename(PathFromUtf8(temp), PathFromUtf8(index_path), ec);
  if (ec) return Status::IoError("browse index rename failed: " + ec.message());
  return Status::Ok();
}

Status WriteManifestBrowseIndex(const std::string& repo_path, uint64_t txn_id,
                                const std::vector<ManifestBrowseRecord>& records) {
  return WriteManifestBrowseIndexToPath(ManifestBrowseIndexPath(repo_path, txn_id),
                                        txn_id, records);
}

Status AppendManifestBrowseIndex(const std::string& repo_path, uint64_t txn_id,
                                 const std::vector<ManifestFileEntry>& files) {
  std::vector<ManifestBrowseRecord> records;
  records.reserve(files.size());
  for (const auto& f : files) {
    ManifestBrowseRecord rec;
    rec.relative_path = f.relative_path;
    rec.txn_id = txn_id;
    rec.size = f.size;
    rec.file_type = f.file_type;
    rec.mtime_unix = f.mtime_unix;
    rec.chunk_count = static_cast<uint32_t>(f.chunk_hashes_hex.size());
    records.push_back(std::move(rec));
  }
  return WriteManifestBrowseIndex(repo_path, txn_id, records);
}

Status BuildManifestBrowseIndexFromSnapshots(
    const std::string& repo_path,
    const std::function<Status(uint64_t txn_id, std::string* manifest_path)>&
        resolve_manifest_path) {
  if (!resolve_manifest_path) {
    return Status::InvalidArgument("resolve_manifest_path is null");
  }
  std::vector<uint64_t> txns;
  const std::string snap_dir = RepoJoinUtf8(repo_path, "snapshots");
  std::error_code ec;
  if (std::filesystem::exists(PathFromUtf8(snap_dir), ec)) {
    for (const auto& ent :
         std::filesystem::directory_iterator(PathFromUtf8(snap_dir), ec)) {
      if (!ent.is_regular_file(ec)) continue;
      const std::string name = PathToUtf8(ent.path().filename());
      if (name.size() > 4 && name.substr(name.size() - 4) == ".man") {
        try {
          txns.push_back(std::stoull(name.substr(0, name.size() - 4)));
        } catch (...) {
        }
      }
    }
  }
  if (txns.empty()) {
    std::string manifest_path;
    const Status st = resolve_manifest_path(0, &manifest_path);
    if (st.ok()) {
      ManifestDocument doc;
      const Status rd = ReadManifestAuto(manifest_path, &doc);
      if (rd.ok() && doc.txn_id > 0) txns.push_back(doc.txn_id);
    }
  }
  std::sort(txns.begin(), txns.end());
  txns.erase(std::unique(txns.begin(), txns.end()), txns.end());
  for (uint64_t txn : txns) {
    std::string manifest_path;
    const Status st = resolve_manifest_path(txn, &manifest_path);
    if (!st.ok()) continue;
    const std::string out_path = ManifestBrowseIndexPath(repo_path, txn);
    const Status build_st =
        BuildManifestBrowseIndexFromFile(manifest_path, txn, out_path);
    if (!build_st.ok()) return build_st;
  }
  return Status::Ok();
}

Status QueryManifestBrowsePage(const std::string& repo_path, uint64_t txn_id,
                               const std::string& prefix, uint64_t offset,
                               uint64_t limit, ManifestFilePage* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->txn_id = txn_id;
  out->offset = offset;
  out->files.clear();
  out->total = 0;

  const std::string index_path = ManifestBrowseIndexPath(repo_path, txn_id);
  std::ifstream in(PathFromUtf8(index_path), std::ios::binary);
  if (!in) return Status::NotFound("browse index missing");

  char magic[8]{};
  in.read(magic, 8);
  uint32_t version = 0;
  uint64_t file_txn = 0;
  uint32_t count = 0;
  in.read(reinterpret_cast<char*>(&version), 4);
  in.read(reinterpret_cast<char*>(&file_txn), 8);
  in.read(reinterpret_cast<char*>(&count), 4);
  if (!in || std::memcmp(magic, kMbiMagic, 8) != 0 || version != kMbiVersion) {
    return Status::Corrupt("browse index bad header");
  }
  if (txn_id != 0 && file_txn != txn_id) {
    return Status::Corrupt("browse index txn mismatch");
  }
  out->txn_id = file_txn;

  if (count == 0) return Status::Ok();

  std::vector<uint64_t> offsets(count);
  for (uint32_t i = 0; i < count; ++i) {
    in.read(reinterpret_cast<char*>(&offsets[i]), 8);
  }
  if (!in) return Status::Corrupt("browse index offset table short");

  if (prefix.empty()) {
    out->total = count;
    if (offset >= count) return Status::Ok();
    const uint64_t end = std::min(static_cast<uint64_t>(count), offset + limit);
    for (uint64_t rank = offset; rank < end; ++rank) {
      const size_t idx = static_cast<size_t>(rank);
      in.seekg(static_cast<std::streamoff>(offsets[idx]));
      in.seekg(0, std::ios::end);
      const auto file_end = in.tellg();
      size_t rec_len = 0;
      if (idx + 1 < count) {
        rec_len = static_cast<size_t>(offsets[idx + 1] - offsets[idx]);
      } else {
        rec_len = static_cast<size_t>(file_end - static_cast<std::streamoff>(offsets[idx]));
      }
      std::vector<uint8_t> buf(rec_len);
      in.seekg(static_cast<std::streamoff>(offsets[idx]));
      in.read(reinterpret_cast<char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
      if (!in) return Status::Corrupt("browse index record read failed");

      ManifestBrowseRecord rec;
      size_t consumed = 0;
      const Status ds = DeserializeRecord(buf.data(), buf.size(), &rec, &consumed);
      if (!ds.ok()) return ds;
      rec.txn_id = file_txn;

      ManifestFileEntry entry;
      entry.relative_path = rec.relative_path;
      entry.size = rec.size;
      entry.file_type = rec.file_type;
      entry.mtime_unix = rec.mtime_unix;
      entry.browse_chunk_count = rec.chunk_count;
      out->files.push_back(std::move(entry));
    }
    return Status::Ok();
  }

  size_t lo = 0;
  size_t hi = count;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const int cmp = ComparePathAtOffset(in, offsets[mid], prefix);
    if (cmp < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  uint64_t matched = 0;
  for (size_t i = lo; i < count; ++i) {
    std::string path;
    if (!ReadPathAtOffset(in, offsets[i], &path)) {
      return Status::Corrupt("browse index path read failed");
    }
    if (!prefix.empty() && path.compare(0, prefix.size(), prefix) != 0) break;
    ++matched;
  }
  out->total = matched;

  if (offset >= matched) return Status::Ok();
  const uint64_t end = std::min(matched, offset + limit);
  for (uint64_t rank = offset; rank < end; ++rank) {
    const size_t idx = lo + static_cast<size_t>(rank);
    in.seekg(static_cast<std::streamoff>(offsets[idx]));
    in.seekg(0, std::ios::end);
    const auto file_end = in.tellg();
    size_t rec_len = 0;
    if (idx + 1 < count) {
      rec_len = static_cast<size_t>(offsets[idx + 1] - offsets[idx]);
    } else {
      rec_len = static_cast<size_t>(file_end - static_cast<std::streamoff>(offsets[idx]));
    }
    std::vector<uint8_t> buf(rec_len);
    in.seekg(static_cast<std::streamoff>(offsets[idx]));
    in.read(reinterpret_cast<char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
    if (!in) return Status::Corrupt("browse index record read failed");

    ManifestBrowseRecord rec;
    size_t consumed = 0;
    const Status ds = DeserializeRecord(buf.data(), buf.size(), &rec, &consumed);
    if (!ds.ok()) return ds;
    rec.txn_id = file_txn;

    ManifestFileEntry entry;
    entry.relative_path = rec.relative_path;
    entry.size = rec.size;
    entry.file_type = rec.file_type;
    entry.mtime_unix = rec.mtime_unix;
    entry.browse_chunk_count = rec.chunk_count;
    out->files.push_back(std::move(entry));
  }
  return Status::Ok();
}

}  // namespace catalog
}  // namespace ebbackup
