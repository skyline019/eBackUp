#include "ebbackup/catalog/snapshot_meta.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/engine/restore_options_json.h"

namespace ebbackup {
namespace catalog {

namespace {

std::string RecordToJsonLine(const SnapshotMetaRecord& rec) {
  std::ostringstream out;
  out << "{\"txn_id\":" << rec.txn_id;
  out << ",\"job_id\":\"" << rec.job_id << "\"";
  out << ",\"retention_tag\":" << rec.retention_tag;
  out << ",\"immutable_until_unix\":" << rec.immutable_until_unix << "}";
  return out.str();
}

Status ParseRecordLine(const std::string& line, SnapshotMetaRecord* out) {
  if (!out) return Status::InvalidArgument("out is null");
  SnapshotMetaRecord rec{};
  uint64_t txn = 0;
  uint64_t tag = 0;
  int64_t until = 0;
  if (!TryReadJsonU64Field(line, "txn_id", &txn)) {
    return Status::Corrupt("snapshot meta missing txn_id");
  }
  (void)ReadJsonStringField(line, "job_id", &rec.job_id);
  (void)TryReadJsonU64Field(line, "retention_tag", &tag);
  rec.retention_tag = static_cast<uint32_t>(tag);
  uint64_t until_u = 0;
  if (TryReadJsonU64Field(line, "immutable_until_unix", &until_u)) {
    until = static_cast<int64_t>(until_u);
  } else {
    int until_i = 0;
    if (TryReadJsonIntField(line, "immutable_until_unix", &until_i)) {
      until = until_i;
    }
  }
  rec.txn_id = txn;
  rec.immutable_until_unix = until;
  *out = std::move(rec);
  return Status::Ok();
}

}  // namespace

std::string SnapshotMetaPath(const std::string& repo_path) {
  return RepoJoinUtf8(repo_path, "catalog/snapshot_meta.jsonl");
}

Status AppendSnapshotMeta(const std::string& repo_path,
                          const SnapshotMetaRecord& record) {
  std::error_code ec;
  std::filesystem::create_directories(
      PathFromUtf8(RepoJoinUtf8(repo_path, "catalog")), ec);
  const std::string path = SnapshotMetaPath(repo_path);
  std::ofstream out(PathFromUtf8(path), std::ios::app);
  if (!out) return Status::IoError("cannot append snapshot meta");
  out << RecordToJsonLine(record) << '\n';
  if (!out) return Status::IoError("snapshot meta append failed");
  return Status::Ok();
}

Status LoadSnapshotMeta(const std::string& repo_path,
                        std::vector<SnapshotMetaRecord>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  const std::string path = SnapshotMetaPath(repo_path);
  if (!std::filesystem::exists(PathFromUtf8(path))) return Status::Ok();
  std::ifstream in(PathFromUtf8(path));
  if (!in) return Status::IoError("cannot read snapshot meta");
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    SnapshotMetaRecord rec{};
    const Status st = ParseRecordLine(line, &rec);
    if (!st.ok()) return st;
    out->push_back(std::move(rec));
  }
  return Status::Ok();
}

Status LoadSnapshotMetaMap(const std::string& repo_path,
                           std::unordered_map<uint64_t, SnapshotMetaRecord>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  std::vector<SnapshotMetaRecord> rows;
  const Status st = LoadSnapshotMeta(repo_path, &rows);
  if (!st.ok()) return st;
  for (const auto& row : rows) {
    (*out)[row.txn_id] = row;
  }
  return Status::Ok();
}

Status FindSnapshotMeta(const std::string& repo_path, uint64_t txn_id,
                        SnapshotMetaRecord* out) {
  if (!out) return Status::InvalidArgument("out is null");
  std::unordered_map<uint64_t, SnapshotMetaRecord> map;
  const Status st = LoadSnapshotMetaMap(repo_path, &map);
  if (!st.ok()) return st;
  const auto it = map.find(txn_id);
  if (it == map.end()) return Status::NotFound("snapshot meta not found");
  *out = it->second;
  return Status::Ok();
}

}  // namespace catalog
}  // namespace ebbackup
