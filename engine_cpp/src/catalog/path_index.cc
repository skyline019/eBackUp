#include "ebbackup/catalog/path_index.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#include "ebbackup/audit/merkle.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"

namespace ebbackup {
namespace catalog {

namespace {

void SkipWs(const std::string& s, size_t* i) {
  while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++(*i);
}

Status ParseJsonString(const std::string& s, size_t* i, std::string* out) {
  SkipWs(s, i);
  if (*i >= s.size() || s[*i] != '"') {
    return Status::InvalidArgument("expected string in json");
  }
  ++(*i);
  std::string value;
  while (*i < s.size()) {
    const char c = s[*i];
    if (c == '"') {
      ++(*i);
      *out = std::move(value);
      return Status::Ok();
    }
    if (c == '\\') {
      ++(*i);
      if (*i >= s.size()) return Status::InvalidArgument("bad json escape");
      const char e = s[(*i)++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          value += e;
          break;
        case 'n':
          value += '\n';
          break;
        case 'r':
          value += '\r';
          break;
        case 't':
          value += '\t';
          break;
        default:
          return Status::InvalidArgument("unsupported json escape");
      }
      continue;
    }
    value += c;
    ++(*i);
  }
  return Status::InvalidArgument("unterminated json string");
}

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

const char* FileTypeJsonName(FileType t) {
  switch (t) {
    case FileType::kDirectory:
      return "dir";
    case FileType::kSymlink:
      return "symlink";
    case FileType::kFifo:
      return "fifo";
    case FileType::kBlock:
      return "block";
    case FileType::kChar:
      return "char";
    default:
      return "file";
  }
}

FileType FileTypeFromJsonName(const std::string& s) {
  if (s == "dir") return FileType::kDirectory;
  if (s == "symlink") return FileType::kSymlink;
  if (s == "fifo") return FileType::kFifo;
  if (s == "block") return FileType::kBlock;
  if (s == "char") return FileType::kChar;
  return FileType::kRegular;
}

Status ParseJsonUint64(const std::string& s, size_t* i, uint64_t* out) {
  SkipWs(s, i);
  if (*i >= s.size()) return Status::InvalidArgument("expected number");
  size_t start = *i;
  while (*i < s.size() && std::isdigit(static_cast<unsigned char>(s[*i]))) ++(*i);
  if (*i == start) return Status::InvalidArgument("expected number");
  try {
    *out = std::stoull(s.substr(start, *i - start));
    return Status::Ok();
  } catch (...) {
    return Status::InvalidArgument("bad number");
  }
}

Status ParseJsonInt64(const std::string& s, size_t* i, int64_t* out) {
  SkipWs(s, i);
  if (*i >= s.size()) return Status::InvalidArgument("expected number");
  size_t start = *i;
  if (s[*i] == '-') ++(*i);
  while (*i < s.size() && std::isdigit(static_cast<unsigned char>(s[*i]))) ++(*i);
  if (*i == start || (s[start] == '-' && *i == start + 1)) {
    return Status::InvalidArgument("expected number");
  }
  try {
    *out = std::stoll(s.substr(start, *i - start));
    return Status::Ok();
  } catch (...) {
    return Status::InvalidArgument("bad number");
  }
}

Status ParseRecordLine(const std::string& line, PathIndexRecord* rec) {
  if (line.empty() || line[0] != '{') return Status::InvalidArgument("bad record");
  size_t i = 1;
  SkipWs(line, &i);
  if (i >= line.size() || line[i] != '"') {
    return Status::InvalidArgument("expected key");
  }
  while (i < line.size()) {
    SkipWs(line, &i);
    if (i >= line.size()) break;
    if (line[i] == '}') {
      ++i;
      return Status::Ok();
    }
    std::string key;
    const Status key_st = ParseJsonString(line, &i, &key);
    if (!key_st.ok()) return key_st;
    SkipWs(line, &i);
    if (i >= line.size() || line[i] != ':') {
      return Status::InvalidArgument("expected colon");
    }
    ++i;
    SkipWs(line, &i);
    if (key == "path") {
      const Status st = ParseJsonString(line, &i, &rec->path);
      if (!st.ok()) return st;
    } else if (key == "content_hash") {
      const Status st = ParseJsonString(line, &i, &rec->content_hash_hex);
      if (!st.ok()) return st;
    } else if (key == "file_type") {
      std::string ft;
      const Status st = ParseJsonString(line, &i, &ft);
      if (!st.ok()) return st;
      rec->file_type = FileTypeFromJsonName(ft);
    } else if (key == "txn_id") {
      const Status st = ParseJsonUint64(line, &i, &rec->txn_id);
      if (!st.ok()) return st;
    } else if (key == "size") {
      const Status st = ParseJsonUint64(line, &i, &rec->size);
      if (!st.ok()) return st;
    } else if (key == "mtime") {
      const Status st = ParseJsonInt64(line, &i, &rec->mtime_unix);
      if (!st.ok()) return st;
    } else {
      return Status::InvalidArgument("unknown key in path index");
    }
    SkipWs(line, &i);
    if (i < line.size() && line[i] == ',') ++i;
  }
  return Status::InvalidArgument("unterminated record");
}

std::string RecordToJsonLine(const PathIndexRecord& rec) {
  std::string j = "{";
  j += "\"path\":";
  JsonEscape(rec.path, &j);
  j += ",\"txn_id\":" + std::to_string(rec.txn_id);
  j += ",\"size\":" + std::to_string(rec.size);
  j += ",\"content_hash\":";
  JsonEscape(rec.content_hash_hex, &j);
  j += ",\"file_type\":\"" + std::string(FileTypeJsonName(rec.file_type)) + '"';
  j += ",\"mtime\":" + std::to_string(rec.mtime_unix);
  j += "}";
  return j;
}

Status LoadAllRecords(const std::string& index_path,
                      std::vector<PathIndexRecord>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  std::ifstream in(PathFromUtf8(index_path));
  if (!in) return Status::Ok();
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    PathIndexRecord rec;
    const Status st = ParseRecordLine(line, &rec);
    if (!st.ok()) return st;
    out->push_back(std::move(rec));
  }
  return Status::Ok();
}

Status WriteAllRecords(const std::string& index_path,
                       const std::vector<PathIndexRecord>& records) {
  std::error_code ec;
  std::filesystem::create_directories(
      PathFromUtf8(PathToUtf8(PathFromUtf8(index_path).parent_path())), ec);
  const std::string temp = index_path + ".new";
  std::ofstream out(PathFromUtf8(temp), std::ios::trunc);
  if (!out) return Status::IoError("cannot write path index");
  for (const auto& rec : records) {
    out << RecordToJsonLine(rec) << '\n';
  }
  if (!out) return Status::IoError("path index write failed");
  out.close();
  std::filesystem::rename(PathFromUtf8(temp), PathFromUtf8(index_path), ec);
  if (ec) return Status::IoError("path index rename failed: " + ec.message());
  return Status::Ok();
}

}  // namespace

std::string PathIndexFilePath(const std::string& repo_path) {
  return RepoJoinUtf8(repo_path, "catalog/path_index.jsonl");
}

std::string ComputeFileContentHashHex(const ManifestFileEntry& file,
                                      DigestAlgo algo) {
  if (file.chunk_hashes_hex.empty()) {
    uint8_t zero[32]{};
    return BytesToHex(zero, 32);
  }
  uint8_t root[32]{};
  const Status st = audit::ComputeMerkleRootForFiles({file}, root, algo);
  if (!st.ok()) return std::string(64, '0');
  return BytesToHex(root, 32);
}

Status AppendManifestToPathIndex(const std::string& repo_path,
                                 const ManifestDocument& doc) {
  const std::string index_path = PathIndexFilePath(repo_path);
  std::error_code ec;
  std::filesystem::create_directories(
      PathFromUtf8(RepoJoinUtf8(repo_path, "catalog")), ec);

  std::ofstream out(PathFromUtf8(index_path), std::ios::app);
  if (!out) return Status::IoError("cannot append path index");

  const DigestAlgo algo = DigestAlgo::kStandard;
  for (const auto& f : doc.files) {
    PathIndexRecord rec;
    rec.path = f.relative_path;
    rec.txn_id = doc.txn_id;
    rec.size = f.size;
    rec.content_hash_hex = ComputeFileContentHashHex(f, algo);
    rec.file_type = f.file_type;
    rec.mtime_unix = f.mtime_unix;
    out << RecordToJsonLine(rec) << '\n';
  }
  if (!out) return Status::IoError("path index append failed");
  return Status::Ok();
}

Status BuildPathIndexFromSnapshots(
    const std::string& repo_path,
    const std::function<Status(uint64_t txn_id, ManifestDocument* out)>&
        load_manifest) {
  if (!load_manifest) return Status::InvalidArgument("load_manifest is null");
  std::vector<PathIndexRecord> all;
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
    ManifestDocument doc;
    const Status st = load_manifest(0, &doc);
    if (st.ok() && doc.txn_id > 0) txns.push_back(doc.txn_id);
  }
  std::sort(txns.begin(), txns.end());
  txns.erase(std::unique(txns.begin(), txns.end()), txns.end());
  for (uint64_t txn : txns) {
    ManifestDocument doc;
    const Status st = load_manifest(txn, &doc);
    if (!st.ok()) continue;
    const DigestAlgo algo = DigestAlgo::kStandard;
    for (const auto& f : doc.files) {
      PathIndexRecord rec;
      rec.path = f.relative_path;
      rec.txn_id = doc.txn_id;
      rec.size = f.size;
      rec.content_hash_hex = ComputeFileContentHashHex(f, algo);
      rec.file_type = f.file_type;
      rec.mtime_unix = f.mtime_unix;
      all.push_back(std::move(rec));
    }
  }
  return WriteAllRecords(PathIndexFilePath(repo_path), all);
}

Status QueryPathHistory(const std::string& repo_path, const std::string& path,
                        std::vector<PathIndexRecord>* out) {
  PathHistoryPage page;
  const Status st = QueryPathHistoryPage(repo_path, path, 0, UINT64_MAX, &page);
  if (!st.ok()) return st;
  *out = std::move(page.records);
  return Status::Ok();
}

Status QueryPathHistoryPage(const std::string& repo_path, const std::string& path,
                            uint64_t offset, uint64_t limit,
                            PathHistoryPage* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->records.clear();
  out->offset = offset;
  out->total = 0;
  std::vector<PathIndexRecord> all;
  const Status load_st = LoadAllRecords(PathIndexFilePath(repo_path), &all);
  if (!load_st.ok()) return load_st;
  std::vector<PathIndexRecord> matched;
  for (const auto& rec : all) {
    if (rec.path == path) matched.push_back(rec);
  }
  std::sort(matched.begin(), matched.end(),
            [](const PathIndexRecord& a, const PathIndexRecord& b) {
              return a.txn_id < b.txn_id;
            });
  out->total = matched.size();
  if (limit == 0) limit = 100;
  if (offset >= matched.size()) return Status::Ok();
  const uint64_t end =
      std::min(matched.size(), static_cast<size_t>(offset + limit));
  for (uint64_t i = offset; i < end; ++i) {
    out->records.push_back(matched[static_cast<size_t>(i)]);
  }
  return Status::Ok();
}

Status ListManifestFilesPage(const ManifestDocument& doc,
                             const std::string& prefix, uint64_t offset,
                             uint64_t limit, ManifestFilePage* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->txn_id = doc.txn_id;
  out->offset = offset;
  out->files.clear();
  std::vector<ManifestFileEntry> sorted = doc.files;
  std::sort(sorted.begin(), sorted.end(),
            [](const ManifestFileEntry& a, const ManifestFileEntry& b) {
              return a.relative_path < b.relative_path;
            });
  std::vector<ManifestFileEntry> filtered;
  for (const auto& f : sorted) {
    if (!prefix.empty() && f.relative_path.compare(0, prefix.size(), prefix) != 0) {
      continue;
    }
    filtered.push_back(f);
  }
  out->total = filtered.size();
  if (offset >= filtered.size()) return Status::Ok();
  const uint64_t end =
      std::min(filtered.size(), static_cast<size_t>(offset + limit));
  for (uint64_t i = offset; i < end; ++i) {
    out->files.push_back(filtered[static_cast<size_t>(i)]);
  }
  return Status::Ok();
}

std::string PathHistoryToJson(const std::vector<PathIndexRecord>& records) {
  std::string j = "{\"ok\":true,\"history\":[";
  for (size_t i = 0; i < records.size(); ++i) {
    if (i) j += ',';
    const auto& r = records[i];
    j += '{';
    j += "\"path\":";
    JsonEscape(r.path, &j);
    j += ",\"txn_id\":" + std::to_string(r.txn_id);
    j += ",\"size\":" + std::to_string(r.size);
    j += ",\"content_hash\":";
    JsonEscape(r.content_hash_hex, &j);
    j += ",\"file_type\":\"" + std::string(FileTypeJsonName(r.file_type)) + '"';
    j += ",\"mtime\":" + std::to_string(r.mtime_unix);
    j += '}';
  }
  j += "],\"count\":" + std::to_string(records.size()) + '}';
  return j;
}

std::string PathHistoryPageToJson(const PathHistoryPage& page) {
  std::string j = "{\"ok\":true";
  j += ",\"total\":" + std::to_string(page.total);
  j += ",\"offset\":" + std::to_string(page.offset);
  j += ",\"history\":[";
  for (size_t i = 0; i < page.records.size(); ++i) {
    if (i) j += ',';
    const auto& r = page.records[i];
    j += '{';
    j += "\"path\":";
    JsonEscape(r.path, &j);
    j += ",\"txn_id\":" + std::to_string(r.txn_id);
    j += ",\"size\":" + std::to_string(r.size);
    j += ",\"content_hash\":";
    JsonEscape(r.content_hash_hex, &j);
    j += ",\"file_type\":\"" + std::string(FileTypeJsonName(r.file_type)) + '"';
    j += ",\"mtime\":" + std::to_string(r.mtime_unix);
    j += '}';
  }
  j += "],\"count\":" + std::to_string(page.records.size()) + '}';
  return j;
}

std::string ManifestPageToJson(const ManifestFilePage& page,
                               const char* index_source) {
  std::string j = "{\"ok\":true,\"txn_id\":" + std::to_string(page.txn_id);
  j += ",\"total\":" + std::to_string(page.total);
  j += ",\"offset\":" + std::to_string(page.offset);
  if (index_source && index_source[0] != '\0') {
    j += ",\"index_source\":\"";
    j += index_source;
    j += '"';
  }
  j += ",\"files\":[";
  for (size_t i = 0; i < page.files.size(); ++i) {
    if (i) j += ',';
    const auto& f = page.files[i];
    j += '{';
    j += "\"relative_path\":";
    JsonEscape(f.relative_path, &j);
    j += ",\"size\":" + std::to_string(f.size);
    j += ",\"file_type\":\"" + std::string(FileTypeJsonName(f.file_type)) + '"';
    j += ",\"mtime_unix\":" + std::to_string(f.mtime_unix);
    j += ",\"chunk_count\":" +
         std::to_string(f.browse_chunk_count != 0
                            ? f.browse_chunk_count
                            : static_cast<uint32_t>(f.chunk_hashes_hex.size()));
    j += '}';
  }
  j += "],\"count\":" + std::to_string(page.files.size()) + '}';
  return j;
}

}  // namespace catalog
}  // namespace ebbackup
