#include "ebbackup/audit/merkle.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "ebbackup/common/digest.h"

namespace ebbackup {
namespace audit {

namespace {

void HashPair(const uint8_t left[32], const uint8_t right[32], uint8_t out[32]) {
  uint8_t buf[64];
  std::memcpy(buf, left, 32);
  std::memcpy(buf + 32, right, 32);
  Sha256(buf, 64, out);
}

Status BuildLeaves(const ManifestDocument& doc,
                   std::vector<std::string>* leaf_hex) {
  if (!leaf_hex) return Status::InvalidArgument("leaf_hex is null");
  leaf_hex->clear();
  std::vector<ManifestFileEntry> files = doc.files;
  std::sort(files.begin(), files.end(),
            [](const ManifestFileEntry& a, const ManifestFileEntry& b) {
              return a.relative_path < b.relative_path;
            });
  for (const auto& file : files) {
    for (const auto& hex : file.chunk_hashes_hex) {
      leaf_hex->push_back(hex);
    }
  }
  return Status::Ok();
}

Status BuildLeavesFromFiles(const std::vector<ManifestFileEntry>& files,
                            std::vector<std::string>* leaf_hex) {
  if (!leaf_hex) return Status::InvalidArgument("leaf_hex is null");
  leaf_hex->clear();
  std::vector<ManifestFileEntry> sorted = files;
  std::sort(sorted.begin(), sorted.end(),
            [](const ManifestFileEntry& a, const ManifestFileEntry& b) {
              return a.relative_path < b.relative_path;
            });
  for (const auto& file : sorted) {
    for (const auto& hex : file.chunk_hashes_hex) {
      leaf_hex->push_back(hex);
    }
  }
  return Status::Ok();
}

Status MerkleFromLeafBytes(const std::vector<std::vector<uint8_t>>& leaves,
                           uint8_t root_out[32]) {
  if (!root_out) return Status::InvalidArgument("root_out is null");
  if (leaves.empty()) {
    std::memset(root_out, 0, 32);
    return Status::Ok();
  }
  std::vector<std::vector<uint8_t>> level = leaves;
  while (level.size() > 1) {
    std::vector<std::vector<uint8_t>> next;
    for (size_t i = 0; i < level.size(); i += 2) {
      const uint8_t* left = level[i].data();
      const uint8_t* right = left;
      std::vector<uint8_t> combined(32);
      if (i + 1 < level.size()) {
        right = level[i + 1].data();
      }
      HashPair(left, right, combined.data());
      next.push_back(std::move(combined));
    }
    level = std::move(next);
  }
  std::memcpy(root_out, level[0].data(), 32);
  return Status::Ok();
}

}  // namespace

Status ComputeMerkleRootFromHashes(const std::vector<std::string>& leaf_hex,
                                   uint8_t root_out[32]) {
  std::vector<std::vector<uint8_t>> leaves;
  leaves.reserve(leaf_hex.size());
  for (const auto& hex : leaf_hex) {
    if (hex.size() != 64) {
      return Status::Corrupt("invalid leaf hash length");
    }
    std::vector<uint8_t> hash(32);
    if (!HexToBytes(hex, hash.data(), 32)) {
      return Status::Corrupt("invalid leaf hash hex");
    }
    leaves.push_back(std::move(hash));
  }
  return MerkleFromLeafBytes(leaves, root_out);
}

Status ComputeMerkleRoot(const ManifestDocument& doc, uint8_t root_out[32]) {
  std::vector<std::string> leaf_hex;
  const Status st = BuildLeaves(doc, &leaf_hex);
  if (!st.ok()) return st;
  return ComputeMerkleRootFromHashes(leaf_hex, root_out);
}

Status ComputeMerkleRootForFiles(const std::vector<ManifestFileEntry>& files,
                                 uint8_t root_out[32]) {
  std::vector<std::string> leaf_hex;
  const Status st = BuildLeavesFromFiles(files, &leaf_hex);
  if (!st.ok()) return st;
  return ComputeMerkleRootFromHashes(leaf_hex, root_out);
}

Status VerifyRestoredFileChunks(const std::string& restored_path,
                                const ManifestFileEntry& manifest,
                                ChunkStore* store) {
  if (!store) return Status::InvalidArgument("store is null");
  std::ifstream in(restored_path, std::ios::binary);
  if (!in) return Status::IoError("cannot read restored file: " + restored_path);
  size_t offset = 0;
  for (const auto& hex : manifest.chunk_hashes_hex) {
    if (hex.size() != 64) {
      return Status::Corrupt("invalid chunk hash length");
    }
    uint8_t expected[32];
    if (!HexToBytes(hex, expected, 32)) {
      return Status::Corrupt("invalid chunk hash hex");
    }
    std::vector<uint8_t> payload;
    const Status get_st = store->Get(expected, &payload);
    if (!get_st.ok()) return get_st;
    const size_t chunk_len = payload.size();
    std::vector<uint8_t> segment(chunk_len);
    in.read(reinterpret_cast<char*>(segment.data()),
            static_cast<std::streamsize>(chunk_len));
    if (!in || static_cast<size_t>(in.gcount()) != chunk_len) {
      return Status::Corrupt("restored file shorter than manifest chunks");
    }
    uint8_t actual[32];
    Sha256(segment.data(), chunk_len, actual);
    if (std::memcmp(actual, expected, 32) != 0) {
      return Status::Corrupt("restored chunk hash mismatch");
    }
    offset += chunk_len;
  }
  if (offset != manifest.size) {
    return Status::Corrupt("restored file size mismatch");
  }
  return Status::Ok();
}

Status ComputeMerkleRootFromRestoredFiles(
    const std::string& dest_root,
    const std::vector<ManifestFileEntry>& files, ChunkStore* store,
    uint8_t root_out[32]) {
  if (!store) return Status::InvalidArgument("store is null");
  std::vector<std::string> leaf_hex;
  std::vector<ManifestFileEntry> sorted = files;
  std::sort(sorted.begin(), sorted.end(),
            [](const ManifestFileEntry& a, const ManifestFileEntry& b) {
              return a.relative_path < b.relative_path;
            });
  for (const auto& file : sorted) {
    if (file.file_type != FileType::kRegular) continue;
    if (file.chunk_hashes_hex.empty()) continue;
    const std::string path =
        (std::filesystem::path(dest_root) / file.relative_path).string();
    const Status verify_st = VerifyRestoredFileChunks(path, file, store);
    if (!verify_st.ok()) return verify_st;
    for (const auto& hex : file.chunk_hashes_hex) {
      leaf_hex.push_back(hex);
    }
  }
  return ComputeMerkleRootFromHashes(leaf_hex, root_out);
}

}  // namespace audit
}  // namespace ebbackup
