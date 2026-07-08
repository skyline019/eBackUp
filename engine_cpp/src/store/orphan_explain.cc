#include "ebbackup/store/orphan_explain.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "ebbackup/common/digest.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/state/backup_phase.h"
#include "ebbackup/store/orphan_gc.h"

namespace ebbackup {
namespace store {

namespace {

constexpr size_t kDefaultSampleLimit = 64;

std::string HashKey(const uint8_t hash[32]) {
  return std::string(reinterpret_cast<const char*>(hash), 32);
}

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

const char* ReasonTag(OrphanReason reason) {
  switch (reason) {
    case OrphanReason::kTombstoned:
      return "tombstoned";
    case OrphanReason::kInterruptedHint:
      return "interrupted_hint";
    default:
      return "unreferenced";
  }
}

Status LoadTombstoneKeys(const ChunkStore& store, std::unordered_set<std::string>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  const std::string path = store.path();
  const std::string tomb_path =
      (std::filesystem::path(path).parent_path() / "tombstones").string();
  std::ifstream in(tomb_path, std::ios::binary);
  if (!in) return Status::Ok();
  std::string line;
  while (std::getline(in, line)) {
    if (line.size() != 64) continue;
    uint8_t hash[32];
    if (!HexToBytes(line, hash, 32)) continue;
    out->insert(HashKey(hash));
  }
  return Status::Ok();
}

void AddSample(OrphanExplainReport* report, OrphanExplainEntry entry,
               uint64_t sample_limit) {
  if (report->samples.size() >= sample_limit) return;
  report->samples.push_back(std::move(entry));
}

}  // namespace

Status BuildOrphanExplainReport(const BackupEngine& engine, uint64_t sample_limit,
                                OrphanExplainReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (sample_limit == 0) sample_limit = kDefaultSampleLimit;
  *out = OrphanExplainReport{};

  const ChunkStore* store = engine.chunk_store();
  if (!store) return Status::InvalidArgument("chunk store unavailable");

  std::unordered_set<std::string> referenced;
  const Status ref_st =
      CollectReferencedHashesForRepo(engine.repo_path(), false, &referenced);
  if (!ref_st.ok()) return ref_st;

  if (engine.phase() == BackupPhase::kAborted &&
      engine.superblock().critical.chunks_written > 0) {
    out->interrupted_hint_count = engine.superblock().critical.chunks_written;
  }

  std::unordered_set<std::string> tombstones;
  const Status tomb_st = LoadTombstoneKeys(*store, &tombstones);
  if (!tomb_st.ok()) return tomb_st;
  out->tombstoned_count = tombstones.size();

  for (const auto& key : tombstones) {
    ++out->total_orphans;
    OrphanExplainEntry entry{};
    entry.chunk_hex = BytesToHex(
        reinterpret_cast<const uint8_t*>(key.data()), key.size());
    entry.reason = OrphanReason::kTombstoned;
    AddSample(out, std::move(entry), sample_limit);
  }

  const Status scan_st = store->ForEachRecord(
      [&](const uint8_t hash[32], uint64_t, uint32_t stored_len) {
        const std::string key = HashKey(hash);
        if (referenced.find(key) != referenced.end()) return Status::Ok();
        ++out->unreferenced_count;
        ++out->total_orphans;
        out->total_orphan_bytes += stored_len;
        OrphanExplainEntry entry{};
        entry.chunk_hex = BytesToHex(hash, 32);
        entry.reason = OrphanReason::kUnreferenced;
        entry.bytes = stored_len;
        AddSample(out, std::move(entry), sample_limit);
        return Status::Ok();
      });
  if (!scan_st.ok()) return scan_st;

  return Status::Ok();
}

std::string OrphanExplainReportToJson(const OrphanExplainReport& report) {
  std::ostringstream out;
  out << "{\"ok\":true";
  out << ",\"total_orphans\":" << report.total_orphans;
  out << ",\"total_orphan_bytes\":" << report.total_orphan_bytes;
  out << ",\"unreferenced_count\":" << report.unreferenced_count;
  out << ",\"tombstoned_count\":" << report.tombstoned_count;
  out << ",\"interrupted_hint_count\":" << report.interrupted_hint_count;
  out << ",\"samples\":[";
  for (size_t i = 0; i < report.samples.size(); ++i) {
    if (i > 0) out << ',';
    const auto& s = report.samples[i];
    std::string esc;
    JsonEscape(s.chunk_hex, &esc);
    out << "{\"chunk_hex\":\"" << esc << '"';
    out << ",\"reason\":\"" << ReasonTag(s.reason) << '"';
    out << ",\"bytes\":" << s.bytes;
    out << ",\"last_referenced_txn\":" << s.last_referenced_txn << '}';
  }
  out << "]}";
  return out.str();
}

}  // namespace store
}  // namespace ebbackup
