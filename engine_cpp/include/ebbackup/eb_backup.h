#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EB_BACKUP_ABI_VERSION 8u

#define EB_BACKUP_FLAG_LZ4 0x0001u
#define EB_BACKUP_FLAG_PIPELINE 0x0002u
#define EB_BACKUP_FLAG_REQUIRE_ANCHOR 0x0004u
#define EB_BACKUP_FLAG_ENCRYPT 0x0008u

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
} EbBackupStats;

EbBackupEngine* eb_backup_open(const char* repo_path);
EbBackupEngine* eb_backup_open_ex(const char* repo_path, EbStatus* err_out);
void eb_backup_close(EbBackupEngine* eng);

EbStatus eb_backup_init_repo(const char* repo_path);
EbStatus eb_backup_run(EbBackupEngine* eng, const char* source_path);
EbStatus eb_backup_run_incremental(EbBackupEngine* eng, const char* source_path);
EbStatus eb_backup_run_ex(EbBackupEngine* eng, const char* source_path,
                          uint32_t flags);
EbStatus eb_backup_run_incremental_ex(EbBackupEngine* eng,
                                      const char* source_path, uint32_t flags);
EbStatus eb_backup_verify(EbBackupEngine* eng);
EbStatus eb_backup_verify_ex(EbBackupEngine* eng, uint32_t flags);
EbStatus eb_backup_recover(EbBackupEngine* eng);
EbStatus eb_backup_restore(EbBackupEngine* eng, const char* dest_path);
EbStatus eb_backup_restore_ex(EbBackupEngine* eng, const char* dest_path,
                              uint32_t flags);
EbStatus eb_backup_gc_orphans(EbBackupEngine* eng, int dry_run);
EbStatus eb_backup_get_stats(EbBackupEngine* eng, EbBackupStats* out);

void eb_backup_set_password(EbBackupEngine* eng, const char* password);
EbStatus eb_backup_load_filter_file(EbBackupEngine* eng, const char* path);

typedef void (*EbProgressFn)(uint64_t pct_permille, void* user_data);
void eb_backup_set_progress(EbBackupEngine* eng, EbProgressFn fn, void* user_data);

uint32_t eb_backup_abi_version(void);

char* eb_backup_last_error(EbBackupEngine* eng);
void eb_backup_free_string(char* s);

#ifdef __cplusplus
}
#endif
