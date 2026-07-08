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

/** JSON: manifest file list for txn_id (0 = latest) */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_list_manifest_files_json(
    EbBackupEngine* eng, uint64_t txn_id, char* out, size_t out_cap);

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

/** JSON: restore with optional filter/remap JSON */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_run_restore_ex_json(
    EbBackupEngine* eng, const char* dest_path, uint64_t txn_id, uint32_t flags,
    const char* filter_json, const char* remap_json, char* out, size_t out_cap);

/** JSON: preview selective restore subset */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_preview_restore_json(
    EbBackupEngine* eng, uint64_t txn_id, const char* filter_json, char* out,
    size_t out_cap);

/** JSON: in-place restore preview (optional filter_json, in_place_options_json) */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_preview_in_place_json(
    EbBackupEngine* eng, uint64_t txn_id, const char* target_root,
    const char* filter_json, const char* in_place_options_json, char* out,
    size_t out_cap);

/** JSON: in-place restore apply (conflict_policy: skip|fail|overwrite) */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_apply_in_place_json(
    EbBackupEngine* eng, uint64_t txn_id, const char* target_root,
    const char* conflict_policy, const char* orphan_policy,
    const char* filter_json, const char* in_place_options_json, char* out,
    size_t out_cap);

/** JSON: export repo delta bundle (repo-level; optional password for encrypt) */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_export_delta_json(
    const char* repo_path, const char* bundle_path, uint64_t base_txn_id,
    uint64_t target_txn_id, const char* password, char* out, size_t out_cap);

/** JSON: import delta bundle into new repo from base + delta */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_import_delta_json(
    const char* base_path, const char* delta_path, const char* out_repo_path,
    char* out, size_t out_cap);

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

/** JSON: maintenance wizard report */
EBBACKUP_WORKBENCH_API int ebbackup_workbench_run_maintenance_wizard_json(
    EbBackupEngine* eng, const char* options_json, char* out, size_t out_cap);

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

EBBACKUP_WORKBENCH_API int ebbackup_workbench_build_path_index_json(
    EbBackupEngine* eng, int full_rebuild, char* out, size_t out_cap);

EBBACKUP_WORKBENCH_API int ebbackup_workbench_query_path_history_json(
    EbBackupEngine* eng, const char* path, uint64_t offset, uint64_t limit,
    char* out, size_t out_cap);

EBBACKUP_WORKBENCH_API int ebbackup_workbench_list_manifest_page_json(
    EbBackupEngine* eng, uint64_t txn_id, const char* prefix, uint64_t offset,
    uint64_t limit, char* out, size_t out_cap);

EBBACKUP_WORKBENCH_API int ebbackup_workbench_diff_snapshots_json(
    EbBackupEngine* eng, uint64_t txn_a, uint64_t txn_b, char* out, size_t out_cap);

EBBACKUP_WORKBENCH_API int ebbackup_workbench_export_restore_report_json(
    EbBackupEngine* eng, char* out, size_t out_cap);

EBBACKUP_WORKBENCH_API int ebbackup_workbench_get_backup_report_json(
    EbBackupEngine* eng, uint64_t txn_id, char* out, size_t out_cap);

EBBACKUP_WORKBENCH_API int ebbackup_workbench_set_backup_hooks_json(
    EbBackupEngine* eng, const char* json, char* out, size_t out_cap);

EBBACKUP_WORKBENCH_API int ebbackup_workbench_init_encrypt_json(const char* repo_path,
                                                                  const char* password,
                                                                  char* out, size_t out_cap);

EBBACKUP_WORKBENCH_API int ebbackup_workbench_unwrap_recovery_key_json(
    EbBackupEngine* eng, const char* recovery_key, char* out, size_t out_cap);

EBBACKUP_WORKBENCH_API int ebbackup_workbench_rotate_password_json(
    EbBackupEngine* eng, const char* old_password, const char* new_password,
    char* out, size_t out_cap);

EBBACKUP_WORKBENCH_API int ebbackup_workbench_upgrade_legacy_envelope_json(
    EbBackupEngine* eng, const char* password, char* out, size_t out_cap);

#ifdef __cplusplus
}
#endif
