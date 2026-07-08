#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {

class BackupEngine;

namespace report {

struct RpoJobSummary {
  std::string job_id;
  std::string name;
  uint64_t last_success_txn{0};
  int64_t last_success_unix{0};
  bool last_report_ok{false};
  uint32_t retention_tag{0};
};

struct RpoSummaryReport {
  uint64_t last_success_txn{0};
  int64_t last_success_unix{0};
  double days_since_last_success{0.0};
  uint64_t snapshot_count{0};
  uint64_t worm_protected_count{0};
  std::vector<RpoJobSummary> jobs;
};

Status BuildRpoSummary(const BackupEngine& engine, RpoSummaryReport* out);

std::string RpoSummaryReportToJson(const RpoSummaryReport& report);

}  // namespace report
}  // namespace ebbackup
