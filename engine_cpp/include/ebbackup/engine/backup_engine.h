#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/chunk/cfi_index.h"
#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/engine/restore_engine.h"
#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/scan/scan_entry.h"
#include "ebbackup/state/backup_phase.h"
#include "ebbackup/state/superblock.h"
#include "ebbackup/state/sync_executor.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/orphan_gc.h"

namespace ebbackup {

enum class BackupMode { kFull, kIncremental };

using ProgressCallback = std::function<void(uint64_t pct_permille, void* user_data)>;

struct BackupOptions {
  bool use_pipeline{false};
  bool use_lz4{false};
  bool require_anchor{false};
  bool use_encryption{false};
  std::string encryption_password;
  BackupFilterOptions filter;
};

struct BackupStats {
  uint64_t files_processed{0};
  uint64_t chunks_written{0};
  uint64_t chunks_reused{0};
  uint64_t chunks_reused_from_cfi{0};
  uint64_t bytes_processed{0};
  uint64_t unexpected_transitions{0};
  uint64_t orphan_chunks_hint{0};
};

class BackupEngine {
 public:
  explicit BackupEngine(std::string repo_path);

  static Status InitRepo(const std::string& repo_path);

  Status Open();
  Status Recover();
  Status RunBackup(const std::string& source_path,
                   BackupMode mode = BackupMode::kFull,
                   const BackupOptions& options = BackupOptions{});
  Status Verify(const BackupOptions& options = BackupOptions{});
  Status Restore(const std::string& dest_path,
                 const RestoreOptions& options = RestoreOptions{});
  Status GcOrphans(bool dry_run, OrphanGcReport* report = nullptr);

  void SetProgressCallback(ProgressCallback cb, void* user_data);

  const BackupSuperBlock& superblock() const { return sb_; }
  const BackupStats& stats() const { return stats_; }
  BackupPhase phase() const { return phase_; }

  BackupSyncExecutor* sync() { return &sync_; }
  ChunkStore* chunk_store() { return chunk_store_.get(); }
  const std::string& repo_path() const { return repo_path_; }

 private:
  Status StartupSelfCheck();
  Status PersistSuperBlock(BackupPhase phase);
  Status DispatchTransition(BackupEvent event);
  Status ScanFiles(const std::string& source_path, const BackupOptions& options);
  Status ChunkPendingFiles(BackupMode mode);
  Status StorePendingChunks(const BackupOptions& options);
  Status RunPipelineBackup(BackupMode mode, const BackupOptions& options);
  Status MergeMetaManifestEntries();
  Status CommitManifestFile();
  Status AppendAuditEntry();
  Status VerifyManifestDocument(const ManifestDocument& doc);
  Status LoadPriorManifest(ManifestDocument* out) const;
  Status SetupEncryption(const BackupOptions& options);
  Status EnsureRepoContentKey(const std::string& password);
  void ClearEncryption();
  void EmitProgress(uint64_t permille);
  Status MaybeTestAbortAfter(BackupPhase phase) const;

  std::string repo_path_;
  BackupSuperBlock sb_{};
  BackupPhase phase_{BackupPhase::kIdle};
  BackupStats stats_{};
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
  std::string source_root_;

  uint32_t last_manifest_crc32_{0};
  uint8_t last_merkle_root_[32]{};
  uint8_t content_key_[32]{};
  bool has_content_key_{false};

  ProgressCallback progress_cb_;
  void* progress_user_{nullptr};
};

void RegisterBackupSyncRules(BackupSyncExecutor* exec, BackupEngine* engine);

}  // namespace ebbackup
