#pragma once

#include <string>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/chunk_compactor.h"
#include "ebbackup/store/orphan_gc.h"
#include "ebbackup/store/repo_stats.h"
#include "ebbackup/store/retention_policy.h"

namespace ebbackup {

struct MaintenanceWizardOptions {
  bool run_prune{true};
  std::string retention_tiers;
  int retain_min{3};
  bool run_gc{true};
  bool run_compact{true};
  bool dry_run_only{false};
  bool verify_after{false};
};

struct MaintenanceWizardReport {
  RepoStats stats_before{};
  RepoStats stats_after{};
  PruneReport prune{};
  OrphanGcReport gc{};
  CompactReport compact{};
  bool compact_skipped{false};
  bool verify_ran{false};
  bool verify_ok{false};
};

Status RunMaintenanceWizard(BackupEngine* engine,
                            const MaintenanceWizardOptions& options,
                            MaintenanceWizardReport* report);

Status ParseMaintenanceWizardJson(const std::string& json,
                                  MaintenanceWizardOptions* out);

}  // namespace ebbackup
