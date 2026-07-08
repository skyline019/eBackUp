#include "ebbackup/catalog/job_report.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/engine/restore_options_json.h"

namespace ebbackup {
namespace catalog {

namespace {

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

std::string RecordToJsonLine(const JobReportLine& rec) {
  std::ostringstream out;
  out << "{\"txn_id\":" << rec.txn_id;
  out << ",\"ts_unix\":" << rec.ts_unix;
  out << ",\"reuse_pct\":" << rec.reuse_pct;
  out << ",\"chunks_written\":" << rec.chunks_written;
  out << ",\"chunks_reused\":" << rec.chunks_reused;
  out << ",\"bytes_processed\":" << rec.bytes_processed;
  out << ",\"backed_up\":" << rec.backed_up;
  out << ",\"skipped\":" << rec.skipped << '}';
  return out.str();
}

Status ParseRecordLine(const std::string& line, JobReportLine* out) {
  if (!out) return Status::InvalidArgument("out is null");
  JobReportLine rec{};
  uint64_t txn = 0;
  if (!TryReadJsonU64Field(line, "txn_id", &txn)) {
    return Status::Corrupt("job report missing txn_id");
  }
  rec.txn_id = txn;
  int64_t ts = 0;
  uint64_t ts_u = 0;
  if (TryReadJsonU64Field(line, "ts_unix", &ts_u)) {
    ts = static_cast<int64_t>(ts_u);
  } else {
    int ts_i = 0;
    if (TryReadJsonIntField(line, "ts_unix", &ts_i)) {
      ts = ts_i;
    }
  }
  rec.ts_unix = ts;
  (void)ParseDoubleField(line, "reuse_pct", &rec.reuse_pct);
  (void)TryReadJsonU64Field(line, "chunks_written", &rec.chunks_written);
  (void)TryReadJsonU64Field(line, "chunks_reused", &rec.chunks_reused);
  (void)TryReadJsonU64Field(line, "bytes_processed", &rec.bytes_processed);
  (void)TryReadJsonU64Field(line, "backed_up", &rec.backed_up);
  (void)TryReadJsonU64Field(line, "skipped", &rec.skipped);
  *out = std::move(rec);
  return Status::Ok();
}

Status LoadAllJobReports(const std::string& repo_path, const std::string& job_id,
                         std::vector<JobReportLine>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  const std::string path = JobReportPath(repo_path, job_id);
  if (!std::filesystem::exists(PathFromUtf8(path))) return Status::Ok();
  std::ifstream in(PathFromUtf8(path));
  if (!in) return Status::IoError("cannot read job reports");
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    JobReportLine rec{};
    const Status st = ParseRecordLine(line, &rec);
    if (!st.ok()) return st;
    out->push_back(std::move(rec));
  }
  return Status::Ok();
}

}  // namespace

std::string JobReportPath(const std::string& repo_path, const std::string& job_id) {
  return RepoJoinUtf8(repo_path, "catalog/jobs/" + job_id + ".jsonl");
}

Status AppendJobReport(const std::string& repo_path, const std::string& job_id,
                       const report::BackupReport& report) {
  if (job_id.empty()) return Status::InvalidArgument("job_id empty");
  std::error_code ec;
  std::filesystem::create_directories(
      PathFromUtf8(RepoJoinUtf8(repo_path, "catalog/jobs")), ec);
  JobReportLine line{};
  line.txn_id = report.txn_id;
  line.ts_unix = static_cast<int64_t>(std::time(nullptr));
  line.reuse_pct = report.reuse_pct;
  line.chunks_written = report.chunks_written;
  line.chunks_reused = report.chunks_reused;
  line.bytes_processed = report.bytes_processed;
  line.backed_up = report.backed_up;
  line.skipped = report.skipped;
  const std::string path = JobReportPath(repo_path, job_id);
  std::ofstream out(PathFromUtf8(path), std::ios::app);
  if (!out) return Status::IoError("cannot append job report");
  out << RecordToJsonLine(line) << '\n';
  if (!out) return Status::IoError("job report append failed");
  return Status::Ok();
}

Status ListJobReports(const std::string& repo_path, const std::string& job_id,
                      uint64_t offset, uint64_t limit,
                      std::vector<JobReportLine>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (job_id.empty()) return Status::InvalidArgument("job_id empty");
  if (limit == 0) limit = 100;
  std::vector<JobReportLine> all;
  const Status st = LoadAllJobReports(repo_path, job_id, &all);
  if (!st.ok()) return st;
  out->clear();
  if (offset >= all.size()) return Status::Ok();
  const size_t end =
      std::min(all.size(), static_cast<size_t>(offset + limit));
  out->assign(all.begin() + static_cast<size_t>(offset), all.begin() + end);
  return Status::Ok();
}

std::string JobReportsToJson(const std::vector<JobReportLine>& lines,
                             uint64_t total, uint64_t offset) {
  std::ostringstream out;
  out << "{\"ok\":true,\"total\":" << total << ",\"offset\":" << offset
      << ",\"reports\":[";
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) out << ',';
    const auto& r = lines[i];
    out << "{\"txn_id\":" << r.txn_id;
    out << ",\"ts_unix\":" << r.ts_unix;
    out << ",\"reuse_pct\":" << r.reuse_pct;
    out << ",\"chunks_written\":" << r.chunks_written;
    out << ",\"chunks_reused\":" << r.chunks_reused;
    out << ",\"bytes_processed\":" << r.bytes_processed;
    out << ",\"backed_up\":" << r.backed_up;
    out << ",\"skipped\":" << r.skipped << '}';
  }
  out << "]}";
  return out.str();
}

}  // namespace catalog
}  // namespace ebbackup
