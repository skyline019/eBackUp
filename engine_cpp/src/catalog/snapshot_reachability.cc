#include "ebbackup/catalog/snapshot_reachability.h"

#include <sstream>
#include <unordered_set>

#include "ebbackup/common/digest.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/store/chunk_store.h"

namespace ebbackup {
namespace catalog {

namespace {

constexpr size_t kMaxMissingSamples = 32;

void JsonEscape(const std::string& s, std::string* out) {
  for (char c : s) {
    switch (c) {
      case '\\':
        out->append("\\\\");
        break;
      case '"':
        out->append("\\\"");
        break;
      default:
        out->push_back(c);
        break;
    }
  }
}

}  // namespace

Status AnalyzeSnapshotReachability(const BackupEngine& engine, uint64_t txn_id,
                                   SnapshotReachabilityReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->txn_id = txn_id;
  out->reachable = false;
  out->files_checked = 0;
  out->chunks_checked = 0;
  out->missing_chunk_count = 0;
  out->missing_chunk_hex.clear();

  if (!engine.chunk_store()) {
    return Status::InvalidArgument("chunk store unavailable");
  }

  ManifestDocument doc;
  const Status rd = engine.LoadManifest(txn_id, &doc);
  if (!rd.ok()) return rd;
  if (txn_id == 0) out->txn_id = doc.txn_id;

  std::unordered_set<std::string> seen_hashes;
  for (const auto& file : doc.files) {
    if (file.file_type != FileType::kRegular) continue;
    ++out->files_checked;
    for (const auto& hex : file.chunk_hashes_hex) {
      if (hex.size() != 64) {
        ++out->missing_chunk_count;
        if (out->missing_chunk_hex.size() < kMaxMissingSamples) {
          out->missing_chunk_hex.push_back(hex);
        }
        continue;
      }
      if (!seen_hashes.insert(hex).second) continue;
      ++out->chunks_checked;
      uint8_t hash[32];
      if (!HexToBytes(hex, hash, 32)) {
        ++out->missing_chunk_count;
        if (out->missing_chunk_hex.size() < kMaxMissingSamples) {
          out->missing_chunk_hex.push_back(hex);
        }
        continue;
      }
      std::vector<uint8_t> payload;
      const Status st = engine.chunk_store()->Get(hash, &payload);
      if (!st.ok()) {
        ++out->missing_chunk_count;
        if (out->missing_chunk_hex.size() < kMaxMissingSamples) {
          out->missing_chunk_hex.push_back(hex);
        }
      }
    }
  }

  out->reachable = out->missing_chunk_count == 0;
  return Status::Ok();
}

std::string SnapshotReachabilityReportToJson(
    const SnapshotReachabilityReport& report) {
  std::ostringstream out;
  out << "{\"ok\":true";
  out << ",\"txn_id\":" << report.txn_id;
  out << ",\"reachable\":" << (report.reachable ? "true" : "false");
  out << ",\"files_checked\":" << report.files_checked;
  out << ",\"chunks_checked\":" << report.chunks_checked;
  out << ",\"missing_chunk_count\":" << report.missing_chunk_count;
  out << ",\"missing_chunks\":[";
  for (size_t i = 0; i < report.missing_chunk_hex.size(); ++i) {
    if (i > 0) out << ',';
    std::string esc;
    JsonEscape(report.missing_chunk_hex[i], &esc);
    out << '"' << esc << '"';
  }
  out << "]}";
  return out.str();
}

}  // namespace catalog
}  // namespace ebbackup
