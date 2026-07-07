#pragma once

#include "ebbackup/eb_backup.h"

#include <stddef.h>

#if defined(_WIN32)
#  if defined(EBBACKUP_WORKBENCH_SHARED_BUILD)
#    define EBBACKUP_WORKBENCH_API __declspec(dllexport)
#  elif defined(EBBACKUP_WORKBENCH_SHARED_USE)
#    define EBBACKUP_WORKBENCH_API __declspec(dllimport)
#  else
#    define EBBACKUP_WORKBENCH_API
#  endif
#elif defined(EBBACKUP_WORKBENCH_SHARED_BUILD)
#  if (defined(__GNUC__) && __GNUC__ >= 4) || defined(__clang__)
#    define EBBACKUP_WORKBENCH_API __attribute__((visibility("default")))
#  else
#    define EBBACKUP_WORKBENCH_API
#  endif
#else
#  define EBBACKUP_WORKBENCH_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** JSON: {"ok":true} or {"ok":false,"error":"..."} */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_init_repo_json(const char* repo_path,
                                                               uint32_t flags,
                                                               char* out, size_t out_cap);

/** JSON: repo path + EbRepoStats fields */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_repo_info_json(EbBackupEngine* eng,
                                                               char* out,
                                                               size_t out_cap);

/** JSON array of snapshot objects */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_list_snapshots_json(EbBackupEngine* eng,
                                                                    char* out,
                                                                    size_t out_cap);

/** JSON: backup stats after run */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_run_backup_json(EbBackupEngine* eng,
                                                                const char* source_path,
                                                                int incremental,
                                                                uint32_t flags,
                                                                char* out,
                                                                size_t out_cap);

/** JSON: restore result */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_run_restore_json(EbBackupEngine* eng,
                                                                 const char* dest_path,
                                                                 uint64_t txn_id,
                                                                 uint32_t flags,
                                                                 char* out,
                                                                 size_t out_cap);

/** JSON: verify result */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_verify_json(EbBackupEngine* eng,
                                                            uint64_t txn_id,
                                                            uint32_t flags,
                                                            char* out,
                                                            size_t out_cap);

/** JSON: recover result */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_recover_json(EbBackupEngine* eng,
                                                             char* out,
                                                             size_t out_cap);

/** JSON: compact report */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_compact_json(EbBackupEngine* eng,
                                                             int dry_run,
                                                             char* out,
                                                             size_t out_cap);

/** JSON: gc orphans report */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_gc_orphans_json(EbBackupEngine* eng,
                                                               int dry_run,
                                                               char* out,
                                                               size_t out_cap);

/** JSON: prune report */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_prune_snapshots_json(
    EbBackupEngine* eng, const char* retention_tiers, int retain_min, int dry_run,
    char* out, size_t out_cap);

/** JSON: {"ok":true,"abi_version":N,"workbench":"ebbackup"} — no engine required */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_runtime_info_json(char* out, size_t out_cap);

/** JSON: {"ok":true,"error":"..."} — last engine error (empty string if none) */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_last_error_json(EbBackupEngine* eng,
                                                                 char* out,
                                                                 size_t out_cap);

/** JSON: current EbBackupStats without running backup */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_get_stats_json(EbBackupEngine* eng,
                                                               char* out,
                                                               size_t out_cap);

#ifdef __cplusplus
}
#endif
