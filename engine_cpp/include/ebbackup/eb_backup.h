#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* All path arguments (repo_path, source_path, dest_path, filter paths, etc.)
 * are UTF-8 encoded, without embedded NUL bytes. */

#define EB_BACKUP_ABI_VERSION 37u

#define EB_BACKUP_FLAG_LZ4 0x0001u
#define EB_BACKUP_FLAG_PIPELINE 0x0002u
#define EB_BACKUP_FLAG_REQUIRE_ANCHOR 0x0004u
#define EB_BACKUP_FLAG_ENCRYPT 0x0008u
#define EB_BACKUP_FLAG_LEGACY_DIGEST 0x0010u
#define EB_BACKUP_FLAG_COMPRESS_AUTO 0x0020u
#define EB_BACKUP_FLAG_COMPRESS_ZSTD 0x0040u
#define EB_BACKUP_FLAG_BALANCED_DURABILITY 0x0080u
#define EB_BACKUP_FLAG_MANIFEST_BINARY 0x0100u
#define EB_BACKUP_INIT_LEGACY 0x0200u
#define EB_BACKUP_FLAG_NO_PIPELINE 0x0400u
#define EB_BACKUP_FLAG_VSS 0x0800u
#define EB_BACKUP_FLAG_VSS_APP 0x1000u
#define EB_BACKUP_INIT_RECOVERY_KEY 0x2000u
#define EB_BACKUP_FLAG_SPARSE_OFF 0x4000u
#define EB_BACKUP_FLAG_EFS_EXPORT_KEYS 0x8000u

#define EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY 0x0001u

typedef enum {
  EB_OK = 0,
  EB_ERROR_INVALID_ARGUMENT,
  EB_ERROR_NOT_FOUND,
  EB_ERROR_CORRUPTED,
  EB_ERROR_IO,
  EB_ERROR_INTERNAL,
  EB_ERROR_CONFLICT,
} EbStatus;

typedef struct EbBackupEngine EbBackupEngine;

typedef struct EbBackupStats {
  uint64_t files_processed;
  uint64_t chunks_written;
  uint64_t chunks_reused;
  uint64_t chunks_reused_from_cfi;
  uint64_t bytes_processed;
  uint64_t orphan_chunks_hint;
  uint64_t content_incompressible_skips;
  uint64_t content_lz4_only;
  uint64_t content_zstd_attempts;
  uint64_t content_zstd_wins;
} EbBackupStats;

typedef struct EbRepoStats {
  uint64_t physical_bytes;
  uint64_t live_bytes;
  uint64_t orphan_bytes;
  uint64_t manifest_bytes;
  uint64_t unique_chunks;
  uint64_t tombstoned_chunks;
  double ampl_ratio;
  uint64_t live_uncompressed_bytes;
  uint64_t live_stored_payload_bytes;
  double compress_ratio;
  uint64_t compressed_chunk_count;
  uint64_t raw_chunk_count;
  uint8_t has_zstd_dict;
  uint64_t zstd_dict_bytes;
} EbRepoStats;

typedef struct EbCompactReport {
  uint64_t physical_before;
  uint64_t physical_after;
  uint64_t live_bytes;
  uint64_t records_copied;
  double ampl_ratio_before;
  double ampl_ratio_after;
} EbCompactReport;

typedef struct EbSnapshotInfo {
  uint64_t txn_id;
  int64_t created_at_unix;
  uint32_t manifest_crc32;
  uint32_t file_count;
} EbSnapshotInfo;

typedef struct EbPruneReport {
  uint64_t kept_count;
  uint64_t pruned_count;
} EbPruneReport;

typedef struct EbOrphanGcReport {
  uint64_t referenced_count;
  uint64_t orphan_count;
  uint64_t tombstoned_count;
} EbOrphanGcReport;

typedef struct EbRestorePreviewReport {
  uint64_t file_count;
  uint64_t dir_count;
  uint64_t total_bytes;
} EbRestorePreviewReport;

typedef struct EbRestoreRemap {
  uint8_t mode; /* 0 keep, 1 strip_prefix, 2 flatten, 3 remap_prefix */
  const char* strip_prefix;
  const char* map_from;
  const char* map_to;
  uint8_t conflict; /* 0 fail, 1 skip, 2 suffix */
} EbRestoreRemap;

typedef struct EbMaintenanceWizardOptions {
  int run_prune;
  const char* retention_tiers;
  int retain_min;
  int run_gc;
  int run_compact;
  int dry_run_only;
  int verify_after;
} EbMaintenanceWizardOptions;

typedef struct EbMaintenanceWizardReport {
  EbRepoStats stats_before;
  EbRepoStats stats_after;
  EbPruneReport prune;
  EbOrphanGcReport gc;
  EbCompactReport compact;
  int compact_skipped;
  int verify_ran;
  int verify_ok;
} EbMaintenanceWizardReport;

typedef struct EbManifestFileInfo {
  char* relative_path;
  uint64_t size;
  uint8_t file_type;
  int64_t mtime_unix;
  uint32_t chunk_count;
} EbManifestFileInfo;

EbBackupEngine* eb_backup_open(const char* repo_path);
EbBackupEngine* eb_backup_open_ex(const char* repo_path, EbStatus* err_out);
void eb_backup_close(EbBackupEngine* eng);

EbStatus eb_backup_init_repo(const char* repo_path);
EbStatus eb_backup_init_repo_ex(const char* repo_path, uint32_t flags);
EbStatus eb_backup_run(EbBackupEngine* eng, const char* source_path);
EbStatus eb_backup_run_incremental(EbBackupEngine* eng, const char* source_path);
EbStatus eb_backup_run_ex(EbBackupEngine* eng, const char* source_path,
                          uint32_t flags);
EbStatus eb_backup_run_incremental_ex(EbBackupEngine* eng,
                                      const char* source_path, uint32_t flags);
EbStatus eb_backup_verify(EbBackupEngine* eng);
EbStatus eb_backup_verify_ex(EbBackupEngine* eng, uint32_t flags);
EbStatus eb_backup_verify_at(EbBackupEngine* eng, uint64_t txn_id);
EbStatus eb_backup_recover(EbBackupEngine* eng);
EbStatus eb_backup_restore(EbBackupEngine* eng, const char* dest_path);
EbStatus eb_backup_restore_ex(EbBackupEngine* eng, const char* dest_path,
                              uint32_t flags);
EbStatus eb_backup_restore_at(EbBackupEngine* eng, const char* dest_path,
                              uint64_t txn_id, uint32_t flags);
EbStatus eb_backup_gc_orphans(EbBackupEngine* eng, int dry_run);
EbStatus eb_backup_gc_orphans_ex(EbBackupEngine* eng, int dry_run,
                                 EbOrphanGcReport* report);
EbStatus eb_backup_compact(EbBackupEngine* eng, int dry_run,
                           EbCompactReport* report);
EbStatus eb_backup_repo_stats(EbBackupEngine* eng, EbRepoStats* out);
EbStatus eb_backup_get_stats(EbBackupEngine* eng, EbBackupStats* out);
EbStatus eb_backup_list_snapshots(EbBackupEngine* eng, EbSnapshotInfo** out,
                                  size_t* count);
void eb_backup_free_snapshots(EbSnapshotInfo* snapshots);
EbStatus eb_backup_list_manifest_files(EbBackupEngine* eng, uint64_t txn_id,
                                       uint64_t* manifest_txn_id,
                                       EbManifestFileInfo** out, size_t* count);
void eb_backup_free_manifest_files(EbManifestFileInfo* files, size_t count);
EbStatus eb_backup_prune_snapshots(EbBackupEngine* eng,
                                     const char* retention_tiers,
                                     int retain_min, int dry_run,
                                     EbPruneReport* report);

void eb_backup_set_password(EbBackupEngine* eng, const char* password);
/** ABI v34: unlock encrypted repo with recovery key (26-char). */
EbStatus eb_backup_unwrap_with_recovery_key(EbBackupEngine* eng,
                                            const char* recovery_key);
/** ABI v34: rotate envelope password; repo must already be unlocked. */
EbStatus eb_backup_rotate_password(EbBackupEngine* eng, const char* old_password,
                                   const char* new_password);
/** ABI v32: crash|app|auto when EB_BACKUP_FLAG_VSS without EB_BACKUP_FLAG_VSS_APP. */
EbStatus eb_backup_set_vss_mode(EbBackupEngine* eng, const char* mode);
/** ABI v32: 0=false, non-zero=true; default true. */
void eb_backup_set_vss_include_junction_volumes(EbBackupEngine* eng, int include);
void eb_backup_set_vss_fallback_live(EbBackupEngine* eng, int enable);
void eb_backup_set_audit_key(EbBackupEngine* eng, const char* audit_key);
void eb_backup_set_backup_hooks(EbBackupEngine* eng, const char* pre_cmd,
                                const char* post_cmd);
/** JSON object: {"plugins":["sqlite_checkpoint",...]} (ABI v27). */
EbStatus eb_backup_set_plugins_json(EbBackupEngine* eng, const char* json);
EbStatus eb_backup_load_filter_file(EbBackupEngine* eng, const char* path);
EbStatus eb_backup_set_filter_json(EbBackupEngine* eng, const char* json);
EbStatus eb_backup_set_restore_remap(EbBackupEngine* eng,
                                       const EbRestoreRemap* remap);
EbStatus eb_backup_preview_restore_at(EbBackupEngine* eng, uint64_t txn_id,
                                      EbRestorePreviewReport* report);
EbStatus eb_backup_run_maintenance_wizard(
    EbBackupEngine* eng, const EbMaintenanceWizardOptions* options,
    EbMaintenanceWizardReport* report);
EbStatus eb_backup_set_filter_json_and_remap(EbBackupEngine* eng,
                                             const char* filter_json,
                                             const char* remap_json);

EbStatus eb_backup_build_path_index(EbBackupEngine* eng, int full_rebuild);
char* eb_backup_query_path_history_json(EbBackupEngine* eng, const char* path,
                                        uint64_t offset, uint64_t limit);
char* eb_backup_list_manifest_files_page_json(EbBackupEngine* eng, uint64_t txn_id,
                                              const char* prefix, uint64_t offset,
                                              uint64_t limit);
char* eb_backup_diff_snapshots_json(EbBackupEngine* eng, uint64_t txn_a,
                                    uint64_t txn_b);
char* eb_backup_export_restore_report_json(EbBackupEngine* eng);
char* eb_backup_get_backup_report_json(EbBackupEngine* eng, uint64_t txn_id);
char* eb_backup_preview_in_place_json(EbBackupEngine* eng, uint64_t txn_id,
                                      const char* target_root,
                                      const char* options_json);
char* eb_backup_apply_in_place_json(EbBackupEngine* eng, uint64_t txn_id,
                                    const char* target_root,
                                    const char* options_json);

char* eb_backup_export_delta_json(const char* repo_path, const char* bundle_path,
                                  uint64_t base_txn_id, uint64_t target_txn_id,
                                  uint32_t flags);
char* eb_backup_import_delta_json(const char* base_path, const char* delta_path,
                                  const char* out_repo_path);
char* eb_backup_apply_delta_json(const char* delta_path, const char* repo_path);

char* eb_backup_list_jobs_json(const char* repo_path);
EbStatus eb_backup_upsert_job_json(const char* repo_path, const char* job_json);
EbStatus eb_backup_delete_job(const char* repo_path, const char* job_id);
EbStatus eb_backup_run_job(EbBackupEngine* eng, const char* job_id, int incremental,
                           uint32_t flags);
char* eb_backup_enqueue_job_json(const char* repo_path, const char* job_json);
char* eb_backup_run_job_queue_json(EbBackupEngine* eng, const char* options_json);
char* eb_backup_job_queue_status_json(const char* repo_path);
char* eb_backup_list_job_reports_json(const char* repo_path, const char* job_id,
                                      uint64_t offset, uint64_t limit);
char* eb_backup_snapshot_reachability_json(EbBackupEngine* eng, uint64_t txn_id);
char* eb_backup_rpo_summary_json(EbBackupEngine* eng);
char* eb_backup_orphan_explain_json(EbBackupEngine* eng, uint64_t sample_limit);
char* eb_backup_append_ops_audit_json(EbBackupEngine* eng, const char* op_json);
char* eb_backup_list_ops_audit_json(EbBackupEngine* eng);

/** JSON in: {"source_path":"...","max_depth":4,"existing":{...}} (ABI v28). */
char* eb_backup_suggest_exclude_filters_json(const char* options_json);

typedef void (*EbProgressFn)(uint64_t pct_permille, void* user_data);
void eb_backup_set_progress(EbBackupEngine* eng, EbProgressFn fn, void* user_data);

uint32_t eb_backup_abi_version(void);

char* eb_backup_last_error(EbBackupEngine* eng);
void eb_backup_free_string(char* s);

/* Returns repo path owned by engine; valid until eb_backup_close. */
const char* eb_backup_engine_repo_path(EbBackupEngine* eng);

#ifdef __cplusplus
}
#endif
