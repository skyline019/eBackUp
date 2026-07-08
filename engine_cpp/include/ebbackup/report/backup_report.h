#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {
namespace report {

struct BackupPathIssue {
  std::string path;
  std::string reason;
};

struct BackupReport {
  uint64_t txn_id{0};
  uint64_t backed_up{0};
  uint64_t skipped{0};
  uint64_t locked{0};
  uint64_t permission_denied{0};
  uint64_t reparse_junction{0};
  uint64_t hook_failed{0};
  uint64_t plugin_skipped{0};
  uint64_t plugin_failed{0};
  uint64_t chunks_written{0};
  uint64_t chunks_reused{0};
  uint64_t bytes_processed{0};
  double reuse_pct{0.0};
  std::string job_id;
  uint32_t retention_tag{0};
  int64_t immutable_until_unix{0};
  std::vector<std::string> plugins;
  std::vector<BackupPathIssue> issues;
  bool durability_downgraded{false};
  bool window_truncated{false};
  int64_t window_end_unix{0};
};

void PopulateReportIssueCounts(BackupReport* report);

std::string BackupReportPath(const std::string& repo_path, uint64_t txn_id);
Status WriteBackupReport(const std::string& repo_path, const BackupReport& report);
Status LoadBackupReport(const std::string& repo_path, uint64_t txn_id,
                        BackupReport* out);
std::string BackupReportToJson(const BackupReport& report);
Status ParseBackupReportJson(const std::string& json, BackupReport* out);

}  // namespace report
}  // namespace ebbackup
