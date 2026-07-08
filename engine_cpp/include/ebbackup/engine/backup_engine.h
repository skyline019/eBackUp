#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ebbackup/catalog/restore_acceptance.h"
#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/chunk/cfi_index.h"
#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/codec/codec_types.h"
#include "ebbackup/codec/content_class.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/engine/restore_engine.h"
#include "ebbackup/pipeline/pipeline_phase_stats.h"
#include "ebbackup/plugin/backup_plugin.h"
#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/scan/scan_entry.h"
#include "ebbackup/job/backup_window.h"
#include "ebbackup/restore/in_place_restore.h"
#include "ebbackup/state/backup_phase.h"
#include "ebbackup/state/superblock.h"
#include "ebbackup/state/sync_executor.h"
#include "ebbackup/store/chunk_compactor.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/orphan_gc.h"
#include "ebbackup/store/repo_stats.h"
#include "ebbackup/store/retention_policy.h"
#include "ebbackup/store/snapshot_store.h"

namespace ebbackup {

enum class BackupMode { kFull, kIncremental };

using ProgressCallback = std::function<void(uint64_t pct_permille, void* user_data)>;

struct RepoInitOptions {
  bool standard_digest{false};
  bool persistent_index{false};
  bool manifest_binary{false};
  bool snapshots{false};
  bool ebpack{false};
  bool coalesced_meta{false};
};

struct BackupOptions {
  bool use_pipeline{false};
  bool disable_pipeline{false};
  bool use_lz4{false};
  bool require_anchor{false};
  bool use_encryption{false};
  std::string encryption_password;
  BackupFilterOptions filter;
  CompressMode compress_mode{CompressMode::kOff};
  uint32_t cpu_budget_permille{1000};
  DurabilityMode durability{DurabilityMode::kStrict};
  ChunkProfileMode chunk_profile{ChunkProfileMode::kAuto};
  uint64_t snapshot_txn_id{0};
  bool gc_latest_manifest_only{false};
  std::string audit_key;
  std::string pre_backup_cmd;
  std::string post_backup_cmd;
  std::string job_id;
  std::vector<std::string> plugins;
  job::BackupWindowPolicy window;
  bool respect_job_windows{true};
  size_t worker_count{0};
  size_t store_shard_count{16};
  bool verify_deep_content{false};
};

struct BackupStats {
  uint64_t files_processed{0};
  uint64_t chunks_written{0};
  uint64_t chunks_reused{0};
  uint64_t chunks_reused_from_cfi{0};
  uint64_t bytes_processed{0};
  uint64_t unexpected_transitions{0};
  uint64_t orphan_chunks_hint{0};
  ContentClassStats content_class{};
};

class BackupEngine {
 public:
  explicit BackupEngine(std::string repo_path);

  static Status InitRepo(const std::string& repo_path,
                         bool standard_digest = false);
  static Status InitRepoEx(const std::string& repo_path,
                           const RepoInitOptions& options);

  Status Open();
  Status Recover();
  Status RunBackup(const std::string& source_path,
                   BackupMode mode = BackupMode::kFull,
                   const BackupOptions& options = BackupOptions{});
  Status RunJob(const std::string& job_id, BackupMode mode = BackupMode::kFull,
                const BackupOptions& options = BackupOptions{});
  Status Verify(const BackupOptions& options = BackupOptions{});
  Status Restore(const std::string& dest_path,
                 const RestoreOptions& options = RestoreOptions{});
  Status PreviewRestore(uint64_t snapshot_txn_id,
                        const RestoreOptions& options,
                        RestorePreviewReport* out) const;
  Status PreviewInPlaceRestore(uint64_t snapshot_txn_id,
                               const std::string& target_root,
                               const RestoreOptions& options,
                               const restore::InPlacePreviewOptions& preview_opts,
                               restore::InPlacePreviewReport* out) const;
  Status ApplyInPlaceRestore(uint64_t snapshot_txn_id,
                             const std::string& target_root,
                             const RestoreOptions& options,
                             const restore::InPlacePreviewOptions& preview_opts,
                             const restore::InPlaceApplyOptions& apply_opts,
                             restore::InPlaceApplyReport* out);
  Status Compact(bool dry_run, CompactReport* report = nullptr);
  Status GcOrphans(bool dry_run, OrphanGcReport* report = nullptr,
                   bool latest_manifest_only = false);
  Status GetRepoStats(RepoStats* out) const;
  Status ListSnapshots(std::vector<SnapshotEntry>* out) const;
  Status LoadManifest(uint64_t snapshot_txn_id, ManifestDocument* out) const;
  Status PruneSnapshots(const RetentionPolicy& policy, bool dry_run,
                        PruneReport* report,
                        const std::string& audit_key = {});

  void SetAuditKey(const std::string& key) { audit_key_ = key; }
  const std::string& audit_key() const { return audit_key_; }

  void SetProgressCallback(ProgressCallback cb, void* user_data);

  Status BuildPathIndex(bool full_rebuild);
  std::string QueryPathHistoryJson(const std::string& path, uint64_t offset = 0,
                                   uint64_t limit = 100) const;
  std::string ListManifestFilesPageJson(uint64_t txn_id, const std::string& prefix,
                                        uint64_t offset, uint64_t limit) const;
  Status EnqueueJob(const std::string& job_id, bool incremental = false,
                    uint32_t flags = 0);
  Status RunJobQueue(bool drain, const BackupOptions& options = BackupOptions{});
  std::string JobQueueStatusJson() const;
  std::string DiffSnapshotsJson(uint64_t txn_a, uint64_t txn_b) const;
  std::string ExportRestoreReportJson() const;
  std::string ExportBackupReportJson(uint64_t txn_id) const;
  std::string SnapshotReachabilityJson(uint64_t txn_id) const;
  std::string RpoSummaryJson() const;
  std::string OrphanExplainJson(uint64_t sample_limit = 64) const;
  std::string AppendOpsAuditJson(const std::string& op_json);
  std::string ListOpsAuditJson() const;
  bool has_restore_acceptance() const { return has_restore_acceptance_; }

  const BackupSuperBlock& superblock() const { return sb_; }
  const BackupStats& stats() const { return stats_; }
  const PipelinePhaseStats& pipeline_phase_stats() const {
    return pipeline_phase_stats_;
  }
  BackupPhase phase() const { return phase_; }
  DigestAlgo digest_algo() const { return digest_algo_; }

  BackupSyncExecutor* sync() { return &sync_; }
  ChunkStore* chunk_store() { return chunk_store_.get(); }
  const ChunkStore* chunk_store() const { return chunk_store_.get(); }
  const std::string& repo_path() const { return repo_path_; }

 private:
  Status StartupSelfCheck();
  Status PersistSuperBlock(BackupPhase phase);
  Status DispatchTransition(BackupEvent event);
  Status ScanFiles(const std::string& source_path, const BackupOptions& options,
                   const std::vector<std::string>& extra_roots,
                   const std::vector<plugin::ScanHint>& scan_hints);
  Status ChunkPendingFiles(BackupMode mode, const BackupOptions& options);
  Status StorePendingChunks(const BackupOptions& options);
  Status RunPipelineBackup(BackupMode mode, const BackupOptions& options);
  Status MergeMetaManifestEntries();
  Status CommitManifestFile();
  Status AppendAuditEntry();
  Status VerifyManifestDocument(const ManifestDocument& doc,
                                uint64_t snapshot_txn_id = 0,
                                bool verify_deep_content = false);
  Status LoadPriorManifest(ManifestDocument* out) const;
  Status SetupEncryption(const BackupOptions& options);
  Status EnsureRepoContentKey(const std::string& password);
  void ClearEncryption();
  void EmitProgress(uint64_t permille);
  Status MaybeTestAbortAfter(BackupPhase phase) const;
  void ResetBackupWindowState();
  void InitBackupWindow(const job::BackupWindowPolicy& policy);
  void MaybeAdaptBackupWindow();
  void TruncatePendingFilesAt(size_t index);

  std::string repo_path_;
  BackupSuperBlock sb_{};
  BackupPhase phase_{BackupPhase::kIdle};
  BackupStats stats_{};
  PipelinePhaseStats pipeline_phase_stats_{};
  mutable BackupSyncExecutor sync_{};
  std::unique_ptr<BackupSuperBlockStore> superblock_store_;
  std::unique_ptr<ChunkStore> chunk_store_;

  std::vector<ScanEntry> pending_scan_entries_;
  std::vector<std::string> pending_files_;
  std::vector<std::vector<uint8_t>> pending_file_bytes_;
  std::vector<std::vector<ChunkDescriptor>> pending_chunks_;
  std::vector<CfiIndex> pending_cfi_;
  std::vector<ManifestFileEntry> pending_manifest_;
  std::vector<ManifestFileEntry> pending_meta_entries_;
  std::vector<report::BackupPathIssue> scan_issues_;
  std::vector<std::string> pending_plugin_reports_;
  std::string source_root_;

  uint32_t last_manifest_crc32_{0};
  uint8_t last_merkle_root_[32]{};
  uint8_t content_key_[32]{};
  bool has_content_key_{false};

  ProgressCallback progress_cb_;
  void* progress_user_{nullptr};
  DigestAlgo digest_algo_{DigestAlgo::kLegacy};
  catalog::RestoreAcceptanceReport last_restore_acceptance_{};
  bool has_restore_acceptance_{false};

  struct ActiveJobContext {
    std::string job_id;
    uint32_t retention_tag{0};
    int64_t immutable_until_unix{0};
  };
  ActiveJobContext active_job_{};
  job::BackupWindowPolicy active_window_{};
  int64_t window_end_unix_{0};
  bool durability_downgraded_{false};
  bool window_truncated_{false};
  std::string audit_key_;
};

void RegisterBackupSyncRules(BackupSyncExecutor* exec, BackupEngine* engine);

}  // namespace ebbackup
