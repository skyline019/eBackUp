#include "ebbackup/audit/rar_chain.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ebbackup/audit/rar_sign.h"
#include "ebbackup/common/crc32.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/fsync.h"

namespace ebbackup {
namespace audit {

namespace {

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '\\') {
      out.append("\\\\");
    } else if (c == '"') {
      out.append("\\\"");
    } else if (c == '\n') {
      out.append("\\n");
    } else if (c == '\r') {
      out.append("\\r");
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool ExtractJsonStringField(const std::string& line, const std::string& key,
                            std::string* out) {
  const std::string needle = "\"" + key + "\":\"";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  size_t i = pos + needle.size();
  std::string value;
  while (i < line.size()) {
    if (line[i] == '"') break;
    if (line[i] == '\\' && i + 1 < line.size()) {
      const char next = line[i + 1];
      if (next == '\\' || next == '"') {
        value.push_back(next);
        i += 2;
        continue;
      }
      if (next == 'n') {
        value.push_back('\n');
        i += 2;
        continue;
      }
      if (next == 'r') {
        value.push_back('\r');
        i += 2;
        continue;
      }
    }
    value.push_back(line[i]);
    ++i;
  }
  *out = std::move(value);
  return true;
}

bool ExtractJsonUint64Field(const std::string& line, const std::string& key,
                            uint64_t* out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  const auto start = pos + needle.size();
  *out = std::strtoull(line.c_str() + start, nullptr, 10);
  return true;
}

bool ExtractJsonInt64Field(const std::string& line, const std::string& key,
                           int64_t* out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  const auto start = pos + needle.size();
  *out = std::strtoll(line.c_str() + start, nullptr, 10);
  return true;
}

RarChainEntry ParseChainLine(const std::string& line) {
  RarChainEntry entry{};
  ExtractJsonUint64Field(line, "sequence", &entry.sequence);
  ExtractJsonUint64Field(line, "txn_id", &entry.txn_id);
  ExtractJsonStringField(line, "manifest_crc32", &entry.manifest_crc32);
  ExtractJsonStringField(line, "merkle_root", &entry.merkle_root);
  ExtractJsonStringField(line, "rar_sha256", &entry.rar_sha256);
  ExtractJsonStringField(line, "prev_rar_sha256", &entry.prev_rar_sha256);
  ExtractJsonInt64Field(line, "generated_at_unix", &entry.generated_at_unix);
  ExtractJsonStringField(line, "body_json", &entry.body_json);
  if (entry.body_json.empty()) entry.body_json = line;
  return entry;
}

}  // namespace

std::string BuildRarBodyJson(uint64_t txn_id, uint32_t manifest_crc32,
                             const uint8_t merkle_root[32]) {
  char crc_buf[16];
  snprintf(crc_buf, sizeof(crc_buf), "%08x",
           static_cast<unsigned>(manifest_crc32));
  std::ostringstream oss;
  oss << "{\"txn_id\":" << txn_id << ",\"manifest_crc32\":\"" << crc_buf
      << "\",\"merkle_root\":\"" << BytesToHex(merkle_root, 32) << "\"}";
  return oss.str();
}

std::string ComputeRarSha256(const std::string& body_json, DigestAlgo algo) {
  uint8_t hash[32];
  ContentHash(algo, reinterpret_cast<const uint8_t*>(body_json.data()),
                body_json.size(), hash);
  return BytesToHex(hash, 32);
}

Status AppendRarChainEntry(const std::string& chain_path,
                           const RarChainEntry& entry) {
  std::filesystem::create_directories(
      std::filesystem::path(chain_path).parent_path());
  std::ofstream stream(chain_path, std::ios::app);
  if (!stream) return Status::IoError("rar chain open failed");
  stream << "{\"sequence\":" << entry.sequence << ",\"txn_id\":" << entry.txn_id
         << ",\"manifest_crc32\":\"" << JsonEscape(entry.manifest_crc32)
         << "\",\"merkle_root\":\"" << JsonEscape(entry.merkle_root)
         << "\",\"rar_sha256\":\"" << JsonEscape(entry.rar_sha256)
         << "\",\"prev_rar_sha256\":\"" << JsonEscape(entry.prev_rar_sha256)
         << "\",\"generated_at_unix\":" << entry.generated_at_unix
         << ",\"body_json\":\"" << JsonEscape(entry.body_json) << "\"";
  if (!entry.signature.empty()) {
    stream << ",\"signature\":\"" << JsonEscape(entry.signature) << "\"";
  }
  stream << "}\n";
  if (!stream) return Status::IoError("rar chain append failed");
  stream.close();
  return FsyncPath(chain_path);
}

Status ReadRarChainEntries(const std::string& chain_path,
                           std::vector<RarChainEntry>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  std::ifstream in(chain_path);
  if (!in) return Status::NotFound("rar chain not found: " + chain_path);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    out->push_back(ParseChainLine(line));
  }
  return Status::Ok();
}

Status ReadLastRarChainEntry(const std::string& chain_path, RarChainEntry* out,
                             bool* found) {
  if (!out || !found) return Status::InvalidArgument("null out/found");
  *found = false;
  std::ifstream in(chain_path, std::ios::binary);
  if (!in) return Status::Ok();
  in.seekg(0, std::ios::end);
  std::streamoff end = in.tellg();
  if (end <= 0) return Status::Ok();
  std::string line;
  for (std::streamoff pos = end - 1; pos >= 0; --pos) {
    in.seekg(pos);
    char c = '\0';
    in.get(c);
    if (c == '\n') {
      if (!line.empty()) break;
      continue;
    }
    if (c != '\r') {
      line.insert(line.begin(), c);
    }
  }
  if (line.empty()) return Status::Ok();
  *out = ParseChainLine(line);
  *found = true;
  return Status::Ok();
}

Status VerifyRarChain(const std::string& chain_path, RarChainVerifyReport* out,
                      DigestAlgo algo) {
  if (!out) return Status::InvalidArgument("out is null");
  out->errors.clear();
  out->consistent = true;
  out->entry_count = 0;
  out->last_sequence = 0;
  out->last_rar_sha256.clear();

  std::vector<RarChainEntry> entries;
  const Status rs = ReadRarChainEntries(chain_path, &entries);
  if (!rs.ok()) return rs;

  std::string prev;
  uint64_t expect_seq = 1;
  for (const auto& entry : entries) {
    ++out->entry_count;
    if (entry.sequence != expect_seq) {
      out->consistent = false;
      out->errors.push_back("sequence gap at " + std::to_string(entry.sequence));
    }
    if (!prev.empty() && entry.prev_rar_sha256 != prev) {
      out->consistent = false;
      out->errors.push_back("prev_rar_sha256 mismatch at seq " +
                            std::to_string(entry.sequence));
    }
    if (!entry.body_json.empty()) {
      const std::string hash = ComputeRarSha256(entry.body_json, algo);
      if (!entry.rar_sha256.empty() && hash != entry.rar_sha256) {
        out->consistent = false;
        out->errors.push_back("rar_sha256 mismatch at seq " +
                              std::to_string(entry.sequence));
      }
    }
    prev = entry.rar_sha256;
    out->last_sequence = entry.sequence;
    out->last_rar_sha256 = entry.rar_sha256;
    ++expect_seq;
  }
  return Status::Ok();
}

std::string RarChainLastSha256(const std::string& chain_path) {
  RarChainEntry last{};
  bool found = false;
  if (!ReadLastRarChainEntry(chain_path, &last, &found).ok() || !found) {
    return {};
  }
  return last.rar_sha256;
}

uint64_t RarChainNextSequence(const std::string& chain_path) {
  RarChainEntry last{};
  bool found = false;
  if (!ReadLastRarChainEntry(chain_path, &last, &found).ok() || !found) {
    return 1;
  }
  return last.sequence + 1;
}

namespace {

std::string TrimPayloadJson(const std::string& payload_json) {
  size_t start = payload_json.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return {};
  size_t end = payload_json.find_last_not_of(" \t\r\n");
  return payload_json.substr(start, end - start + 1);
}

bool IsOpsBody(const std::string& body_json) {
  return body_json.find("\"kind\":\"ops\"") != std::string::npos ||
         body_json.find("\"kind\": \"ops\"") != std::string::npos;
}

}  // namespace

std::string BuildOpsBodyJson(const std::string& op,
                             const std::string& payload_json) {
  const std::string op_esc = JsonEscape(op);
  std::ostringstream oss;
  oss << "{\"kind\":\"ops\",\"op\":\"" << op_esc << '"';
  if (!payload_json.empty()) {
    const std::string trimmed = TrimPayloadJson(payload_json);
    if (!trimmed.empty() && trimmed.front() == '{') {
      oss << ',';
      if (trimmed.size() >= 2) {
        oss << trimmed.substr(1, trimmed.size() - 2);
      }
    }
  }
  oss << '}';
  return oss.str();
}

Status AppendOpsAuditEntry(const std::string& repo_path, const std::string& op,
                           const std::string& payload_json, DigestAlgo algo,
                           const std::string& audit_key) {
  const std::string chain_path =
      (std::filesystem::path(repo_path) / "audit/rar.chain").string();
  const std::string body_json = BuildOpsBodyJson(op, payload_json);
  RarChainEntry entry{};
  entry.sequence = RarChainNextSequence(chain_path);
  entry.txn_id = 0;
  entry.manifest_crc32 = "00000000";
  entry.merkle_root = std::string(64, '0');
  entry.prev_rar_sha256 = RarChainLastSha256(chain_path);
  entry.generated_at_unix = static_cast<int64_t>(std::time(nullptr));
  entry.body_json = body_json;
  entry.rar_sha256 = ComputeRarSha256(body_json, algo);
  if (!audit_key.empty()) {
    (void)SignRarJson(body_json, audit_key, &entry.signature);
  }
  return AppendRarChainEntry(chain_path, entry);
}

Status ListOpsAuditEntries(const std::string& repo_path,
                           std::vector<RarChainEntry>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  const std::string chain_path =
      (std::filesystem::path(repo_path) / "audit/rar.chain").string();
  std::vector<RarChainEntry> all;
  const Status st = ReadRarChainEntries(chain_path, &all);
  if (!st.ok()) return st;
  for (const auto& entry : all) {
    if (IsOpsBody(entry.body_json)) out->push_back(entry);
  }
  return Status::Ok();
}

std::string OpsAuditEntriesToJson(const std::vector<RarChainEntry>& entries) {
  std::ostringstream out;
  out << "{\"ok\":true,\"entries\":[";
  for (size_t i = 0; i < entries.size(); ++i) {
    if (i > 0) out << ',';
    const auto& e = entries[i];
    const std::string body_esc = JsonEscape(e.body_json);
    out << "{\"sequence\":" << e.sequence;
    out << ",\"generated_at_unix\":" << e.generated_at_unix;
    out << ",\"body_json\":\"" << body_esc << '"';
    if (!e.signature.empty()) {
      out << ",\"signature\":\"" << JsonEscape(e.signature) << '"';
    }
    out << '}';
  }
  out << "]}";
  return out.str();
}

}  // namespace audit
}  // namespace ebbackup
