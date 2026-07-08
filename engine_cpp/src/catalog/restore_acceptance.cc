#include "ebbackup/catalog/restore_acceptance.h"

#include <algorithm>

#include "ebbackup/audit/merkle.h"
#include "ebbackup/audit/merkle_proof.h"
#include "ebbackup/engine/manifest.h"

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

}  // namespace

Status BuildRestoreAcceptanceReport(
    const std::vector<std::string>& restored_paths,
    const std::string& subset_merkle_hex, uint64_t txn_id, uint64_t total_bytes,
    bool verify_ok, RestoreAcceptanceReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->txn_id = txn_id;
  out->subset_merkle_root_hex = subset_merkle_hex;
  out->file_count = restored_paths.size();
  out->paths = restored_paths;
  out->verify_ok = verify_ok ? 1 : 0;
  out->total_bytes = total_bytes;
  out->merkle_proofs.clear();
  return Status::Ok();
}

Status BuildRestoreAcceptanceReportWithFiles(
    const std::vector<std::pair<std::string, ManifestFileEntry>>& restored_files,
    const std::string& subset_merkle_hex, uint64_t txn_id, uint64_t total_bytes,
    bool verify_ok, DigestAlgo algo, RestoreAcceptanceReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  std::vector<std::string> paths;
  paths.reserve(restored_files.size());
  for (const auto& [path, file] : restored_files) {
    (void)file;
    paths.push_back(path);
  }
  std::sort(paths.begin(), paths.end());
  const Status base = BuildRestoreAcceptanceReport(paths, subset_merkle_hex, txn_id,
                                                   total_bytes, verify_ok, out);
  if (!base.ok()) return base;

  std::vector<ManifestFileEntry> files;
  files.reserve(restored_files.size());
  for (const auto& [path, file] : restored_files) {
    (void)path;
    if (file.file_type == FileType::kRegular && !file.chunk_hashes_hex.empty()) {
      files.push_back(file);
    }
  }
  std::sort(files.begin(), files.end(),
            [](const ManifestFileEntry& a, const ManifestFileEntry& b) {
              return a.relative_path < b.relative_path;
            });

  std::vector<std::string> all_leaves;
  for (const auto& file : files) {
    for (const auto& h : file.chunk_hashes_hex) all_leaves.push_back(h);
  }

  for (const auto& [dest_path, file] : restored_files) {
    if (file.file_type != FileType::kRegular || file.chunk_hashes_hex.empty()) {
      continue;
    }
    RestoreAcceptanceProofEntry entry;
    entry.path = dest_path;
    entry.leaf_hex = file.chunk_hashes_hex.front();
    uint8_t file_root[32]{};
    if (audit::ComputeMerkleRootForFiles({file}, file_root, algo).ok()) {
      entry.subset_merkle_hex = BytesToHex(file_root, 32);
    }
    size_t leaf_index = 0;
    for (size_t i = 0; i < all_leaves.size(); ++i) {
      if (all_leaves[i] == entry.leaf_hex) {
        leaf_index = i;
        break;
      }
    }
    (void)audit::GenerateMerkleProof(all_leaves, leaf_index, algo,
                                     &entry.merkle_proof);
    out->merkle_proofs.push_back(std::move(entry));
  }
  return Status::Ok();
}

std::string RestoreAcceptanceReportToJson(const RestoreAcceptanceReport& report) {
  std::string j = "{\"ok\":true";
  j += ",\"txn_id\":" + std::to_string(report.txn_id);
  j += ",\"subset_merkle_root\":";
  JsonEscape(report.subset_merkle_root_hex, &j);
  j += ",\"file_count\":" + std::to_string(report.file_count);
  j += ",\"total_bytes\":" + std::to_string(report.total_bytes);
  j += ",\"verify_ok\":" + std::to_string(report.verify_ok);
  j += ",\"paths\":[";
  for (size_t i = 0; i < report.paths.size(); ++i) {
    if (i) j += ',';
    JsonEscape(report.paths[i], &j);
  }
  j += "],\"issues\":[";
  for (size_t i = 0; i < report.issues.size(); ++i) {
    if (i) j += ',';
    j += '{';
    j += "\"path\":";
    JsonEscape(report.issues[i].path, &j);
    j += ",\"reason\":";
    JsonEscape(report.issues[i].reason, &j);
    j += '}';
  }
  j += "],\"merkle_proofs\":[";
  for (size_t i = 0; i < report.merkle_proofs.size(); ++i) {
    if (i) j += ',';
    const auto& p = report.merkle_proofs[i];
    j += '{';
    j += "\"path\":";
    JsonEscape(p.path, &j);
    j += ",\"leaf\":";
    JsonEscape(p.leaf_hex, &j);
    j += ",\"subset_merkle\":";
    JsonEscape(p.subset_merkle_hex, &j);
    j += ",\"merkle_proof\":";
    j += audit::MerkleProofToJson(p.merkle_proof);
    j += '}';
  }
  j += "]}";
  return j;
}

}  // namespace catalog
}  // namespace ebbackup
