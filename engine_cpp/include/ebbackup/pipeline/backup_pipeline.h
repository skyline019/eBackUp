#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/codec/content_class.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/status.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/pipeline/pipeline_phase_stats.h"
#include "ebbackup/report/backup_report.h"
#include "ebbackup/store/chunk_store.h"

namespace ebbackup {

struct BackupPipelineOptions {
  bool use_lz4{false};
  bool use_encryption{false};
  const uint8_t* content_key{nullptr};
  bool use_mmap{true};
  size_t queue_depth{32};
  size_t worker_count{0};
  size_t store_shard_count{16};
  DigestAlgo digest_algo{DigestAlgo::kLegacy};
  CompressMode compress_mode{CompressMode::kOff};
  uint32_t cpu_budget_permille{1000};
  ChunkProfileMode chunk_profile{ChunkProfileMode::kAuto};
  DurabilityMode durability{DurabilityMode::kStrict};
  ContentClassStats* content_stats{nullptr};
  PipelinePhaseStats* phase_stats{nullptr};
  std::vector<report::BackupPathIssue>* scan_issues{nullptr};
};

struct BackupPipelineResult {
  std::vector<ManifestFileEntry> manifest_files;
};

Status RunBackupPipeline(const std::vector<std::string>& file_paths,
                           const std::string& source_root, BackupMode mode,
                           const ManifestDocument* prior_manifest,
                           ChunkStore* chunk_store, BackupStats* stats,
                           const BackupPipelineOptions& options,
                           BackupPipelineResult* result,
                           ProgressCallback progress_cb = nullptr,
                           void* progress_user = nullptr);

}  // namespace ebbackup
