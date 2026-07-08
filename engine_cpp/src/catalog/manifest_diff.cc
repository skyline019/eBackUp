#include "ebbackup/catalog/manifest_diff.h"

#include <algorithm>
#include <map>
#include <set>

#include "ebbackup/audit/merkle.h"
#include "ebbackup/audit/merkle_proof.h"
#include "ebbackup/catalog/path_index.h"
#include "ebbackup/common/digest.h"

namespace ebbackup {
namespace catalog {

namespace {

void JsonEscape(const std::string& s, std::string* out) {
  *out += '"';
  for (char c : s) {
    switch (c) {
      case '"':
        *out += "\\\"";
        break;
      case '\\':
        *out += "\\\\";
        break;
      case '\n':
        *out += "\\n";
        break;
      case '\r':
        *out += "\\r";
        break;
      case '\t':
        *out += "\\t";
        break;
      default:
        *out += c;
        break;
    }
  }
  *out += '"';
}

std::map<std::string, ManifestFileEntry> IndexByPath(
    const ManifestDocument& doc) {
  std::map<std::string, ManifestFileEntry> m;
  for (const auto& f : doc.files) {
    m[f.relative_path] = f;
  }
  return m;
}

std::vector<std::string> ChunkLeavesForFile(const ManifestFileEntry& file) {
  return file.chunk_hashes_hex;
}

void AttachFileSubsetProof(const ManifestFileEntry& file, DigestAlgo algo,
                           std::string* merkle_hex,
                           std::vector<audit::MerkleProofStep>* proof) {
  if (!merkle_hex || !proof) return;
  proof->clear();
  const std::vector<std::string> leaves = ChunkLeavesForFile(file);
  if (leaves.empty()) {
    *merkle_hex = std::string(64, '0');
    return;
  }
  uint8_t root[32]{};
  if (!audit::ComputeMerkleRootFromHashes(leaves, root, algo).ok()) return;
  *merkle_hex = BytesToHex(root, 32);
  (void)audit::GenerateMerkleProof(leaves, 0, algo, proof);
}

double ComputeChunkReuseRatio(const ManifestDocument& a,
                              const ManifestDocument& b) {
  std::set<std::string> chunks_a;
  std::set<std::string> chunks_b;
  const auto idx_a = IndexByPath(a);
  const auto idx_b = IndexByPath(b);
  for (const auto& kv : idx_a) {
    for (const auto& h : kv.second.chunk_hashes_hex) chunks_a.insert(h);
  }
  for (const auto& kv : idx_b) {
    for (const auto& h : kv.second.chunk_hashes_hex) chunks_b.insert(h);
  }
  if (chunks_b.empty()) return 0.0;
  size_t shared = 0;
  for (const auto& h : chunks_b) {
    if (chunks_a.count(h)) ++shared;
  }
  return static_cast<double>(shared) / static_cast<double>(chunks_b.size());
}

std::vector<std::string> ChangedChunkLeaves(
    const std::vector<ManifestFileEntry>& changed) {
  std::vector<std::string> leaves;
  std::vector<ManifestFileEntry> sorted = changed;
  std::sort(sorted.begin(), sorted.end(),
            [](const ManifestFileEntry& a, const ManifestFileEntry& b) {
              return a.relative_path < b.relative_path;
            });
  for (const auto& file : sorted) {
    for (const auto& h : file.chunk_hashes_hex) leaves.push_back(h);
  }
  return leaves;
}

}  // namespace

Status DiffManifestDocuments(const ManifestDocument& a, const ManifestDocument& b,
                             DigestAlgo algo, SnapshotDiffResult* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->txn_a = a.txn_id;
  out->txn_b = b.txn_id;
  out->added.clear();
  out->removed.clear();
  out->modified.clear();
  out->diff_merkle_proof.clear();
  const auto idx_a = IndexByPath(a);
  const auto idx_b = IndexByPath(b);
  for (const auto& kv : idx_b) {
    const auto it = idx_a.find(kv.first);
    if (it == idx_a.end()) {
      SnapshotDiffAddedEntry added;
      added.path = kv.first;
      AttachFileSubsetProof(kv.second, algo, &added.subset_merkle_hex,
                            &added.merkle_proof);
      out->added.push_back(std::move(added));
      continue;
    }
    const std::string hash_a = ComputeFileContentHashHex(it->second, algo);
    const std::string hash_b = ComputeFileContentHashHex(kv.second, algo);
    if (hash_a != hash_b || it->second.size != kv.second.size) {
      ManifestDiffEntry entry;
      entry.path = kv.first;
      entry.size_a = it->second.size;
      entry.size_b = kv.second.size;
      entry.content_hash_a = hash_a;
      entry.content_hash_b = hash_b;
      AttachFileSubsetProof(kv.second, algo, &entry.subset_merkle_hex,
                            &entry.merkle_proof);
      out->modified.push_back(std::move(entry));
    }
  }
  for (const auto& kv : idx_a) {
    if (idx_b.find(kv.first) == idx_b.end()) {
      out->removed.push_back(kv.first);
    }
  }
  std::sort(out->added.begin(), out->added.end(),
            [](const SnapshotDiffAddedEntry& x, const SnapshotDiffAddedEntry& y) {
              return x.path < y.path;
            });
  std::sort(out->removed.begin(), out->removed.end());
  std::sort(out->modified.begin(), out->modified.end(),
            [](const ManifestDiffEntry& x, const ManifestDiffEntry& y) {
              return x.path < y.path;
            });
  out->chunk_reuse_ratio = ComputeChunkReuseRatio(a, b);

  std::vector<ManifestFileEntry> changed;
  for (const auto& e : out->added) changed.push_back(idx_b.at(e.path));
  for (const auto& m : out->modified) changed.push_back(idx_b.at(m.path));
  const std::vector<std::string> diff_leaves = ChangedChunkLeaves(changed);
  if (!diff_leaves.empty()) {
    uint8_t diff_root[32]{};
    if (audit::ComputeMerkleRootFromHashes(diff_leaves, diff_root, algo).ok()) {
      out->diff_merkle_root_hex = BytesToHex(diff_root, 32);
      (void)audit::GenerateMerkleProof(diff_leaves, 0, algo,
                                       &out->diff_merkle_proof);
    }
  }
  return Status::Ok();
}

std::string SnapshotDiffToJson(const SnapshotDiffResult& diff) {
  std::string j = "{\"ok\":true";
  j += ",\"txn_a\":" + std::to_string(diff.txn_a);
  j += ",\"txn_b\":" + std::to_string(diff.txn_b);
  j += ",\"chunk_reuse_ratio\":" + std::to_string(diff.chunk_reuse_ratio);
  j += ",\"diff_merkle_root\":";
  JsonEscape(diff.diff_merkle_root_hex, &j);
  j += ",\"diff_merkle_proof\":";
  j += audit::MerkleProofToJson(diff.diff_merkle_proof);
  j += ",\"added\":[";
  for (size_t i = 0; i < diff.added.size(); ++i) {
    if (i) j += ',';
    const auto& a = diff.added[i];
    j += '{';
    j += "\"path\":";
    JsonEscape(a.path, &j);
    j += ",\"subset_merkle\":";
    JsonEscape(a.subset_merkle_hex, &j);
    j += ",\"merkle_proof\":";
    j += audit::MerkleProofToJson(a.merkle_proof);
    j += '}';
  }
  j += "],\"removed\":[";
  for (size_t i = 0; i < diff.removed.size(); ++i) {
    if (i) j += ',';
    JsonEscape(diff.removed[i], &j);
  }
  j += "],\"modified\":[";
  for (size_t i = 0; i < diff.modified.size(); ++i) {
    if (i) j += ',';
    const auto& m = diff.modified[i];
    j += '{';
    j += "\"path\":";
    JsonEscape(m.path, &j);
    j += ",\"size_a\":" + std::to_string(m.size_a);
    j += ",\"size_b\":" + std::to_string(m.size_b);
    j += ",\"content_hash_a\":";
    JsonEscape(m.content_hash_a, &j);
    j += ",\"content_hash_b\":";
    JsonEscape(m.content_hash_b, &j);
    j += ",\"subset_merkle\":";
    JsonEscape(m.subset_merkle_hex, &j);
    j += ",\"merkle_proof\":";
    j += audit::MerkleProofToJson(m.merkle_proof);
    j += '}';
  }
  j += "]}";
  return j;
}

}  // namespace catalog
}  // namespace ebbackup
