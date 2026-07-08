#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/report/backup_report.h"

namespace ebbackup {
namespace catalog {

struct JobReportLine {
  uint64_t txn_id{0};
  int64_t ts_unix{0};
  double reuse_pct{0.0};
  uint64_t chunks_written{0};
  uint64_t chunks_reused{0};
  uint64_t bytes_processed{0};
  uint64_t backed_up{0};
  uint64_t skipped{0};
};

std::string JobReportPath(const std::string& repo_path, const std::string& job_id);
Status AppendJobReport(const std::string& repo_path, const std::string& job_id,
                       const report::BackupReport& report);
Status ListJobReports(const std::string& repo_path, const std::string& job_id,
                      uint64_t offset, uint64_t limit,
                      std::vector<JobReportLine>* out);
std::string JobReportsToJson(const std::vector<JobReportLine>& lines,
                             uint64_t total, uint64_t offset);

}  // namespace catalog
}  // namespace ebbackup
