#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/engine/restore_engine.h"
#include "ebbackup/report/backup_report.h"

namespace ebbackup {

class BackupEngine;

namespace restore {

struct InPlacePreviewOptions {
  uint64_t base_txn_id{0};
  bool use_three_way{true};
};

struct InPlacePreviewEntry {
  std::string path;
  std::string action;
  std::string reason;
  std::string base_action;
  std::string live_state;
};

struct InPlacePreviewSummary {
  uint64_t add_count{0};
  uint64_t modify_count{0};
  uint64_t unchanged_count{0};
  uint64_t conflict_count{0};
  uint64_t both_changed_count{0};
  uint64_t skip_count{0};
  uint64_t orphan_count{0};
  uint64_t bytes_to_write{0};
};

struct InPlacePreviewReport {
  uint64_t txn_id{0};
  uint64_t base_txn_id{0};
  bool three_way{false};
  std::string target_root;
  InPlacePreviewSummary summary;
  std::vector<InPlacePreviewEntry> entries;
};

enum class InPlaceConflictPolicy : uint8_t {
  kSkip = 0,
  kFail = 1,
  kOverwrite = 2,
};

enum class InPlaceOrphanPolicy : uint8_t {
  kSkip = 0,
  kDelete = 1,
};

struct InPlaceApplyOptions {
  InPlaceConflictPolicy conflict{InPlaceConflictPolicy::kSkip};
  InPlaceOrphanPolicy orphan{InPlaceOrphanPolicy::kSkip};
  bool dry_run{false};
};

struct InPlaceApplySummary {
  uint64_t applied_count{0};
  uint64_t skipped_count{0};
  uint64_t failed_count{0};
  uint64_t overwritten_count{0};
  uint64_t add_count{0};
  uint64_t modify_count{0};
  uint64_t unchanged_count{0};
  uint64_t conflict_count{0};
  uint64_t both_changed_count{0};
  uint64_t orphan_count{0};
  uint64_t orphan_deleted_count{0};
  uint64_t bytes_written{0};
};

struct InPlaceApplyReport {
  uint64_t txn_id{0};
  uint64_t base_txn_id{0};
  bool three_way{false};
  bool dry_run{false};
  std::string target_root;
  InPlaceApplySummary summary;
  std::vector<InPlacePreviewEntry> entries;
  std::vector<report::BackupPathIssue> issues;
};

struct InPlacePlannedEntry {
  ManifestFileEntry file;
  std::string dest_rel;
  InPlacePreviewEntry preview;
};

Status BuildInPlacePlan(const BackupEngine& engine, uint64_t txn_id,
                        const std::string& target_root,
                        const RestoreOptions& options,
                        const InPlacePreviewOptions& preview_opts,
                        std::vector<InPlacePlannedEntry>* out,
                        uint64_t* resolved_base_txn = nullptr,
                        bool* three_way_used = nullptr,
                        uint64_t* resolved_target_txn = nullptr);

Status PreviewInPlaceRestore(const BackupEngine& engine, uint64_t txn_id,
                             const std::string& target_root,
                             const RestoreOptions& options,
                             const InPlacePreviewOptions& preview_opts,
                             InPlacePreviewReport* out);

Status ApplyInPlaceRestore(BackupEngine& engine, uint64_t txn_id,
                           const std::string& target_root,
                           const RestoreOptions& restore_opts,
                           const InPlacePreviewOptions& preview_opts,
                           const InPlaceApplyOptions& apply_opts,
                           InPlaceApplyReport* out);

std::string InPlacePreviewReportToJson(const InPlacePreviewReport& report);
std::string InPlaceApplyReportToJson(const InPlaceApplyReport& report);

}  // namespace restore
}  // namespace ebbackup
