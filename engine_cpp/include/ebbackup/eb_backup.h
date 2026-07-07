#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* All path arguments (repo_path, source_path, dest_path, filter paths, etc.)
 * are UTF-8 encoded, without embedded NUL bytes. */

#define EB_BACKUP_ABI_VERSION 12u

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
EbStatus eb_backup_compact(EbBackupEngine* eng, int dry_run,
                           EbCompactReport* report);
EbStatus eb_backup_repo_stats(EbBackupEngine* eng, EbRepoStats* out);
EbStatus eb_backup_get_stats(EbBackupEngine* eng, EbBackupStats* out);
EbStatus eb_backup_list_snapshots(EbBackupEngine* eng, EbSnapshotInfo** out,
                                  size_t* count);
void eb_backup_free_snapshots(EbSnapshotInfo* snapshots);
EbStatus eb_backup_prune_snapshots(EbBackupEngine* eng,
                                     const char* retention_tiers,
                                     int retain_min, int dry_run,
                                     EbPruneReport* report);

void eb_backup_set_password(EbBackupEngine* eng, const char* password);
void eb_backup_set_audit_key(EbBackupEngine* eng, const char* audit_key);
EbStatus eb_backup_load_filter_file(EbBackupEngine* eng, const char* path);

typedef void (*EbProgressFn)(uint64_t pct_permille, void* user_data);
void eb_backup_set_progress(EbBackupEngine* eng, EbProgressFn fn, void* user_data);

uint32_t eb_backup_abi_version(void);

char* eb_backup_last_error(EbBackupEngine* eng);
void eb_backup_free_string(char* s);

#ifdef __cplusplus
}
#endif
