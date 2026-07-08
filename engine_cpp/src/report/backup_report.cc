#include "ebbackup/report/backup_report.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/engine/restore_options_json.h"

namespace ebbackup {
namespace report {

namespace {

void JsonEscape(const std::string& s, std::string* out) {
  if (!out) return;
  for (char c : s) {
    switch (c) {
      case '\\':
        out->append("\\\\");
        break;
      case '"':
        out->append("\\\"");
        break;
      case '\n':
        out->append("\\n");
        break;
      case '\r':
        out->append("\\r");
        break;
      case '\t':
        out->append("\\t");
        break;
      default:
        out->push_back(c);
        break;
    }
  }
}

std::string Trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

bool ParseU64Field(const std::string& json, const char* key, uint64_t* out) {
  if (!out) return false;
  const std::string needle = std::string("\"") + key + "\":";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  size_t i = pos + needle.size();
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
  size_t j = i;
  while (j < json.size() && (std::isdigit(static_cast<unsigned char>(json[j])) ||
                             json[j] == '.' || json[j] == '-')) {
    ++j;
  }
  *out = static_cast<uint64_t>(std::strtoull(json.substr(i, j - i).c_str(), nullptr, 10));
  return true;
}

bool ParseDoubleField(const std::string& json, const char* key, double* out) {
  if (!out) return false;
  const std::string needle = std::string("\"") + key + "\":";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  size_t i = pos + needle.size();
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
  size_t j = i;
  while (j < json.size() &&
         (std::isdigit(static_cast<unsigned char>(json[j])) || json[j] == '.' ||
          json[j] == '-' || json[j] == 'e' || json[j] == 'E' || json[j] == '+')) {
    ++j;
  }
  *out = std::strtod(json.substr(i, j - i).c_str(), nullptr);
  return true;
}

bool ParseJsonObjectArray(const std::string& json, const char* key,
                          std::vector<std::string>* out) {
  if (!out) return false;
  out->clear();
  const std::string needle = std::string("\"") + key + "\"";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return true;
  const size_t arr_start = json.find('[', pos);
  if (arr_start == std::string::npos) return false;
  size_t i = arr_start + 1;
  while (i < json.size()) {
    while (i < json.size() && json[i] != '{') {
      if (json[i] == ']') return true;
      ++i;
    }
    if (i >= json.size() || json[i] != '{') break;
    int depth = 0;
    size_t obj_start = i;
    for (; i < json.size(); ++i) {
      if (json[i] == '{') ++depth;
      else if (json[i] == '}') {
        --depth;
        if (depth == 0) {
          out->push_back(json.substr(obj_start, i - obj_start + 1));
          ++i;
          break;
        }
      }
    }
  }
  return true;
}

bool ParseVssWritersArray(const std::string& json,
                          std::vector<BackupReport::VssWriterReportEntry>* out) {
  if (!out) return false;
  out->clear();
  std::vector<std::string> objects;
  if (!ParseJsonObjectArray(json, "vss_writers", &objects)) return false;
  for (const std::string& obj : objects) {
    BackupReport::VssWriterReportEntry entry{};
    (void)ReadJsonStringField(obj, "id", &entry.id);
    (void)ReadJsonStringField(obj, "name", &entry.name);
    (void)ReadJsonStringField(obj, "state", &entry.state);
    out->push_back(std::move(entry));
  }
  return true;
}

bool ParseIssuesArray(const std::string& json, std::vector<BackupPathIssue>* out) {
  if (!out) return false;
  out->clear();
  const size_t pos = json.find("\"issues\"");
  if (pos == std::string::npos) return true;
  const size_t arr_start = json.find('[', pos);
  if (arr_start == std::string::npos) return false;
  size_t i = arr_start + 1;
  while (i < json.size()) {
    while (i < json.size() && json[i] != '{') {
      if (json[i] == ']') return true;
      ++i;
    }
    if (i >= json.size() || json[i] != '{') break;
    const size_t obj_end = json.find('}', i);
    if (obj_end == std::string::npos) break;
    const std::string obj = json.substr(i, obj_end - i + 1);
    BackupPathIssue issue{};
    const size_t path_key = obj.find("\"path\"");
    if (path_key != std::string::npos) {
      const size_t q1 = obj.find('"', path_key + 6);
      const size_t q2 = obj.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos) {
        issue.path = obj.substr(q1 + 1, q2 - q1 - 1);
      }
    }
    const size_t reason_key = obj.find("\"reason\"");
    if (reason_key != std::string::npos) {
      const size_t q1 = obj.find('"', reason_key + 8);
      const size_t q2 = obj.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos) {
        issue.reason = obj.substr(q1 + 1, q2 - q1 - 1);
      }
    }
    out->push_back(std::move(issue));
    i = obj_end + 1;
  }
  return true;
}

}  // namespace

void PopulateReportIssueCounts(BackupReport* report) {
  if (!report) return;
  report->skipped = 0;
  report->locked = 0;
  report->permission_denied = 0;
  report->reparse_junction = 0;
  report->hook_failed = 0;
  report->plugin_skipped = 0;
  report->plugin_failed = 0;
  for (const auto& issue : report->issues) {
    if (issue.reason == "locked") {
      ++report->locked;
    } else if (issue.reason == "permission_denied") {
      ++report->permission_denied;
    } else if (issue.reason == "reparse_junction") {
      ++report->reparse_junction;
      ++report->skipped;
    } else if (issue.reason.rfind("hook_failed:", 0) == 0) {
      ++report->hook_failed;
      ++report->skipped;
    } else if (issue.reason.rfind("plugin_skipped:platform:", 0) == 0 ||
               issue.reason.rfind("plugin_unknown:", 0) == 0) {
      ++report->plugin_skipped;
      ++report->skipped;
    } else if (issue.reason.rfind("plugin_quiesce_failed:", 0) == 0) {
      ++report->plugin_failed;
      ++report->skipped;
    } else if (issue.reason == "unreadable" || issue.reason == "depth_exceeded" ||
               issue.reason == "symlink_loop") {
      ++report->skipped;
    } else if (issue.reason == "window_truncated") {
      ++report->skipped;
    } else if (issue.reason.rfind("vss_unavailable:", 0) == 0) {
      ++report->skipped;
    }
  }
}

std::string BackupReportPath(const std::string& repo_path, uint64_t txn_id) {
  return RepoJoinUtf8(repo_path, "reports/" + std::to_string(txn_id) + ".json");
}

std::string BackupReportToJson(const BackupReport& report) {
  std::ostringstream out;
  out << "{\"ok\":true";
  out << ",\"txn_id\":" << report.txn_id;
  out << ",\"backed_up\":" << report.backed_up;
  out << ",\"skipped\":" << report.skipped;
  out << ",\"locked\":" << report.locked;
  out << ",\"permission_denied\":" << report.permission_denied;
  out << ",\"reparse_junction\":" << report.reparse_junction;
  out << ",\"hook_failed\":" << report.hook_failed;
  if (report.plugin_skipped != 0) {
    out << ",\"plugin_skipped\":" << report.plugin_skipped;
  }
  if (report.plugin_failed != 0) {
    out << ",\"plugin_failed\":" << report.plugin_failed;
  }
  out << ",\"chunks_written\":" << report.chunks_written;
  out << ",\"chunks_reused\":" << report.chunks_reused;
  out << ",\"bytes_processed\":" << report.bytes_processed;
  out << ",\"reuse_pct\":" << report.reuse_pct;
  if (!report.job_id.empty()) {
    std::string esc;
    JsonEscape(report.job_id, &esc);
    out << ",\"job_id\":\"" << esc << "\"";
  }
  if (report.retention_tag != 0) {
    out << ",\"retention_tag\":" << report.retention_tag;
  }
  if (report.immutable_until_unix != 0) {
    out << ",\"immutable_until_unix\":" << report.immutable_until_unix;
  }
  if (report.durability_downgraded) {
    out << ",\"durability_downgraded\":true";
  }
  if (report.window_truncated) {
    out << ",\"window_truncated\":true";
  }
  if (report.window_end_unix != 0) {
    out << ",\"window_end_unix\":" << report.window_end_unix;
  }
  if (report.vss_used) {
    out << ",\"vss_used\":true";
    if (!report.vss_consistency.empty()) {
      std::string esc;
      JsonEscape(report.vss_consistency, &esc);
      out << ",\"vss_consistency\":\"" << esc << "\"";
    }
    if (!report.vss_snapshot_set_id.empty()) {
      std::string esc;
      JsonEscape(report.vss_snapshot_set_id, &esc);
      out << ",\"vss_snapshot_set_id\":\"" << esc << "\"";
    }
    if (!report.vss_volumes.empty()) {
      out << ",\"vss_volumes\":[";
      for (size_t i = 0; i < report.vss_volumes.size(); ++i) {
        if (i) out << ',';
        std::string esc;
        JsonEscape(report.vss_volumes[i], &esc);
        out << "\"" << esc << "\"";
      }
      out << "]";
    }
    if (!report.vss_mode.empty()) {
      std::string esc;
      JsonEscape(report.vss_mode, &esc);
      out << ",\"vss_mode\":\"" << esc << "\"";
    }
    if (report.vss_cross_volume) {
      out << ",\"vss_cross_volume\":true";
    }
    if (!report.vss_shadow_storage_ok) {
      out << ",\"vss_shadow_storage_ok\":false";
    }
    if (!report.vss_writers.empty()) {
      out << ",\"vss_writers\":[";
      for (size_t i = 0; i < report.vss_writers.size(); ++i) {
        if (i) out << ',';
        const auto& w = report.vss_writers[i];
        std::string id_esc;
        std::string name_esc;
        std::string state_esc;
        JsonEscape(w.id, &id_esc);
        JsonEscape(w.name, &name_esc);
        JsonEscape(w.state, &state_esc);
        out << "{\"id\":\"" << id_esc << "\",\"name\":\"" << name_esc
            << "\",\"state\":\"" << state_esc << "\"}";
      }
      out << "]";
    }
  }
  if (report.sparse_file_count != 0) {
    out << ",\"sparse_file_count\":" << report.sparse_file_count;
  }
  if (report.efs_skipped_count != 0) {
    out << ",\"efs_skipped_count\":" << report.efs_skipped_count;
  }
  if (!report.recovery_key_issued.empty()) {
    std::string rk_esc;
    JsonEscape(report.recovery_key_issued, &rk_esc);
    out << ",\"recovery_key_issued\":\"" << rk_esc << "\"";
  }
  if (!report.vss_shadow_storage_bytes.empty()) {
    out << ",\"vss_shadow_storage_bytes\":[";
    for (size_t i = 0; i < report.vss_shadow_storage_bytes.size(); ++i) {
      if (i) out << ',';
      out << report.vss_shadow_storage_bytes[i];
    }
    out << "]";
  }
  if (!report.plugins.empty()) {
    out << ",\"plugins\":[";
    for (size_t i = 0; i < report.plugins.size(); ++i) {
      if (i) out << ',';
      out << report.plugins[i];
    }
    out << "]";
  }
  out << ",\"issues\":[";
  for (size_t i = 0; i < report.issues.size(); ++i) {
    if (i) out << ',';
    std::string path_esc;
    std::string reason_esc;
    JsonEscape(report.issues[i].path, &path_esc);
    JsonEscape(report.issues[i].reason, &reason_esc);
    out << "{\"path\":\"" << path_esc << "\",\"reason\":\"" << reason_esc << "\"}";
  }
  out << "]}";
  return out.str();
}

Status WriteBackupReport(const std::string& repo_path, const BackupReport& report) {
  std::error_code ec;
  std::filesystem::create_directories(
      PathFromUtf8(RepoJoinUtf8(repo_path, "reports")), ec);
  const std::string path = BackupReportPath(repo_path, report.txn_id);
  std::ofstream out(PathFromUtf8(path), std::ios::trunc);
  if (!out) return Status::IoError("cannot write backup report");
  out << BackupReportToJson(report);
  return Status::Ok();
}

Status ParseBackupReportJson(const std::string& json, BackupReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  BackupReport report{};
  (void)ParseU64Field(json, "txn_id", &report.txn_id);
  (void)ParseU64Field(json, "backed_up", &report.backed_up);
  (void)ParseU64Field(json, "skipped", &report.skipped);
  (void)ParseU64Field(json, "locked", &report.locked);
  (void)ParseU64Field(json, "permission_denied", &report.permission_denied);
  (void)ParseU64Field(json, "reparse_junction", &report.reparse_junction);
  (void)ParseU64Field(json, "hook_failed", &report.hook_failed);
  (void)ParseU64Field(json, "plugin_skipped", &report.plugin_skipped);
  (void)ParseU64Field(json, "plugin_failed", &report.plugin_failed);
  (void)ParseU64Field(json, "chunks_written", &report.chunks_written);
  (void)ParseU64Field(json, "chunks_reused", &report.chunks_reused);
  (void)ParseU64Field(json, "bytes_processed", &report.bytes_processed);
  (void)ParseDoubleField(json, "reuse_pct", &report.reuse_pct);
  (void)ReadJsonStringField(json, "job_id", &report.job_id);
  uint64_t tag = 0;
  if (ParseU64Field(json, "retention_tag", &tag)) {
    report.retention_tag = static_cast<uint32_t>(tag);
  }
  uint64_t until = 0;
  if (ParseU64Field(json, "immutable_until_unix", &until)) {
    report.immutable_until_unix = static_cast<int64_t>(until);
  }
  (void)ReadJsonBoolField(json, "durability_downgraded", &report.durability_downgraded);
  (void)ReadJsonBoolField(json, "window_truncated", &report.window_truncated);
  (void)ReadJsonBoolField(json, "vss_used", &report.vss_used);
  (void)ReadJsonStringField(json, "vss_consistency", &report.vss_consistency);
  (void)ReadJsonStringField(json, "vss_mode", &report.vss_mode);
  (void)ReadJsonStringField(json, "vss_snapshot_set_id", &report.vss_snapshot_set_id);
  (void)ReadJsonStringArrayField(json, "vss_volumes", &report.vss_volumes);
  (void)ReadJsonBoolField(json, "vss_cross_volume", &report.vss_cross_volume);
  bool shadow_ok = true;
  if (ReadJsonBoolField(json, "vss_shadow_storage_ok", &shadow_ok).ok()) {
    report.vss_shadow_storage_ok = shadow_ok;
  }
  (void)ParseVssWritersArray(json, &report.vss_writers);
  (void)ParseU64Field(json, "sparse_file_count", &report.sparse_file_count);
  (void)ParseU64Field(json, "efs_skipped_count", &report.efs_skipped_count);
  (void)ReadJsonStringField(json, "recovery_key_issued", &report.recovery_key_issued);
  (void)ReadJsonU64ArrayField(json, "vss_shadow_storage_bytes",
                              &report.vss_shadow_storage_bytes);
  if (ParseU64Field(json, "window_end_unix", &until)) {
    report.window_end_unix = static_cast<int64_t>(until);
  }
  if (!ParseJsonObjectArray(json, "plugins", &report.plugins)) {
    return Status::Corrupt("backup report plugins parse failed");
  }
  if (!ParseIssuesArray(json, &report.issues)) {
    return Status::Corrupt("backup report issues parse failed");
  }
  *out = std::move(report);
  return Status::Ok();
}

Status LoadBackupReport(const std::string& repo_path, uint64_t txn_id,
                        BackupReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  const std::string path = BackupReportPath(repo_path, txn_id);
  if (!std::filesystem::exists(PathFromUtf8(path))) {
    return Status::NotFound("backup report not found");
  }
  std::ifstream in(PathFromUtf8(path), std::ios::binary);
  if (!in) return Status::IoError("cannot read backup report");
  std::ostringstream ss;
  ss << in.rdbuf();
  const Status st = ParseBackupReportJson(ss.str(), out);
  if (!st.ok()) return st;
  if (out->txn_id == 0) out->txn_id = txn_id;
  return Status::Ok();
}

}  // namespace report
}  // namespace ebbackup
