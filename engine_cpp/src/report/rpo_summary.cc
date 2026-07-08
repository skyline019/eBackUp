#include "ebbackup/report/rpo_summary.h"

#include <algorithm>
#include <unordered_map>
#include <chrono>
#include <cmath>
#include <sstream>

#include "ebbackup/catalog/job_report.h"
#include "ebbackup/catalog/snapshot_meta.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/job/job_config.h"
#include "ebbackup/store/snapshot_store.h"

namespace ebbackup {
namespace report {

namespace {

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

int64_t NowUnix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

Status BuildRpoSummary(const BackupEngine& engine, RpoSummaryReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  *out = RpoSummaryReport{};

  std::vector<SnapshotEntry> snaps;
  const Status snap_st = engine.ListSnapshots(&snaps);
  if (!snap_st.ok()) return snap_st;
  out->snapshot_count = snaps.size();
  if (!snaps.empty()) {
    const auto best = std::max_element(
        snaps.begin(), snaps.end(),
        [](const SnapshotEntry& a, const SnapshotEntry& b) {
          return a.created_at_unix < b.created_at_unix;
        });
    out->last_success_txn = best->txn_id;
    out->last_success_unix = best->created_at_unix;
  }

  std::unordered_map<uint64_t, catalog::SnapshotMetaRecord> meta_map;
  const Status meta_st =
      catalog::LoadSnapshotMetaMap(engine.repo_path(), &meta_map);
  if (meta_st.ok()) {
    const int64_t now = NowUnix();
    for (const auto& kv : meta_map) {
      if (kv.second.immutable_until_unix > now) {
        ++out->worm_protected_count;
      }
    }
  }

  std::vector<job::BackupJob> jobs;
  const Status jobs_st = job::LoadJobs(engine.repo_path(), &jobs);
  if (jobs_st.ok()) {
    for (const auto& job : jobs) {
      RpoJobSummary js{};
      js.job_id = job.id;
      js.name = job.name.empty() ? job.id : job.name;
      js.retention_tag = job.retention_tag;

      std::vector<catalog::JobReportLine> lines;
      const Status rep_st =
          catalog::ListJobReports(engine.repo_path(), job.id, 0, 1, &lines);
      if (rep_st.ok() && !lines.empty()) {
        js.last_success_txn = lines.front().txn_id;
        js.last_success_unix = lines.front().ts_unix;
        js.last_report_ok = lines.front().backed_up > 0 || lines.front().txn_id > 0;
      }

      for (const auto& snap : snaps) {
        const auto it = meta_map.find(snap.txn_id);
        if (it != meta_map.end() && it->second.job_id == job.id) {
          if (snap.created_at_unix >= js.last_success_unix) {
            js.last_success_txn = snap.txn_id;
            js.last_success_unix = snap.created_at_unix;
            js.last_report_ok = true;
          }
        }
      }

      out->jobs.push_back(std::move(js));
    }
  }

  if (out->last_success_unix > 0) {
    const double seconds =
        static_cast<double>(NowUnix() - out->last_success_unix);
    out->days_since_last_success = seconds / 86400.0;
  } else {
    out->days_since_last_success = -1.0;
  }

  return Status::Ok();
}

std::string RpoSummaryReportToJson(const RpoSummaryReport& report) {
  std::ostringstream out;
  out << "{\"ok\":true";
  out << ",\"last_success_txn\":" << report.last_success_txn;
  out << ",\"last_success_unix\":" << report.last_success_unix;
  out << ",\"days_since_last_success\":" << report.days_since_last_success;
  out << ",\"snapshot_count\":" << report.snapshot_count;
  out << ",\"worm_protected_count\":" << report.worm_protected_count;
  out << ",\"jobs\":[";
  for (size_t i = 0; i < report.jobs.size(); ++i) {
    if (i > 0) out << ',';
    const auto& j = report.jobs[i];
    std::string id_esc;
    std::string name_esc;
    JsonEscape(j.job_id, &id_esc);
    JsonEscape(j.name, &name_esc);
    out << "{\"job_id\":\"" << id_esc << '"';
    out << ",\"name\":\"" << name_esc << '"';
    out << ",\"last_success_txn\":" << j.last_success_txn;
    out << ",\"last_success_unix\":" << j.last_success_unix;
    out << ",\"last_report_ok\":" << (j.last_report_ok ? "true" : "false");
    out << ",\"retention_tag\":" << j.retention_tag << '}';
  }
  out << "]}";
  return out.str();
}

}  // namespace report
}  // namespace ebbackup
