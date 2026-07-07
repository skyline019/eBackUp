#pragma once

#include <atomic>
#include <string>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/retention_policy.h"

namespace ebbackup {

struct ScheduleConfig {
  int interval_seconds{3600};
  std::string source_path;
  std::string repo_base;
  int retain_count{3};
  BackupOptions backup_options{};
  std::string encryption_password;
  RetentionPolicy retention_policy{};
  bool auto_prune{true};
  bool auto_gc_after_prune{false};
};

Status LoadScheduleConfig(const std::string& config_path, ScheduleConfig* out);

Status LoadScheduleConfigAuto(const std::string& config_path, ScheduleConfig* out);

Status RunScheduledBackup(const ScheduleConfig& config, int max_runs = -1);

Status RunWatchBackup(const std::string& source_path, const std::string& repo_path,
                      const BackupOptions& options, int debounce_ms = 2000,
                      int max_triggers = -1);

std::string ScheduleRepoPath(const std::string& repo_base);

#if defined(_MSC_VER)
__declspec(deprecated("use ScheduleRepoPath; repo-* rotation is legacy"))
#endif
std::string
#if !defined(_MSC_VER)
[[deprecated("use ScheduleRepoPath; repo-* rotation is legacy")]]
#endif
MakeRotatedRepoPath(const std::string& repo_base);

void PruneRotatedRepos(const std::string& repo_base, int retain_count);

}  // namespace ebbackup
