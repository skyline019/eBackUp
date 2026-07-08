#include "ebbackup/eb_backup.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/restore_options_json.h"
#include "ebbackup/archive/eb_bundle.h"
#include "ebbackup/catalog/job_report.h"
#include "ebbackup/job/job_config.h"
#include "ebbackup/queue/job_queue.h"
#include "ebbackup/restore/in_place_restore.h"
#include "ebbackup/engine/restore_options_json.h"
#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/scan/exclude_suggestions.h"
#include "ebbackup/store/maintenance_wizard.h"

namespace {

std::string g_bundle_password;

EbStatus ToEbStatus(const ebbackup::Status& st) {
  if (st.ok()) return EB_OK;
  switch (st.code()) {
    case ebbackup::StatusCode::kInvalidArgument:
      return EB_ERROR_INVALID_ARGUMENT;
    case ebbackup::StatusCode::kNotFound:
      return EB_ERROR_NOT_FOUND;
    case ebbackup::StatusCode::kCorrupt:
      return EB_ERROR_CORRUPTED;
    case ebbackup::StatusCode::kIoError:
      return EB_ERROR_IO;
    case ebbackup::StatusCode::kConflict:
      return EB_ERROR_CONFLICT;
    default:
      return EB_ERROR_INTERNAL;
  }
}

struct EbBackupEngineImpl {
  std::unique_ptr<ebbackup::BackupEngine> engine;
  std::string last_error;
  std::string password;
  std::string audit_key;
  ebbackup::BackupFilterOptions filter;
  ebbackup::RestorePathRemap remap;
  ebbackup::SymlinkRemap symlink_remap;
  ebbackup::winmeta::AclRestorePolicy acl_policy{};
  ebbackup::winmeta::ReparseRestorePolicy reparse_policy{};
  std::string pre_backup_cmd;
  std::string post_backup_cmd;
  std::vector<std::string> plugins;
  ebbackup::winmeta::VssConsistencyMode vss_mode_override{
      ebbackup::winmeta::VssConsistencyMode::kCrash};
  bool vss_mode_override_set{false};
  bool vss_include_junction_override_set{false};
  bool vss_include_junction_volumes{true};
  bool vss_fallback_live_set{false};
  bool vss_fallback_live{false};
  EbProgressFn progress_fn{nullptr};
  void* progress_user{nullptr};
};

ebbackup::RestoreOptions BuildRestoreOptions(const EbBackupEngineImpl* impl,
                                             uint32_t flags,
                                             uint64_t txn_id = 0) {
  ebbackup::RestoreOptions options{};
  options.encryption_password = impl->password;
  options.filter = impl->filter;
  options.path_remap = impl->remap;
  options.symlink_remap = impl->symlink_remap;
  options.acl_policy = impl->acl_policy;
  options.reparse_policy = impl->reparse_policy;
  options.snapshot_txn_id = txn_id;
  if (flags & EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY) {
    options.verify_subset_merkle = false;
    options.verify_restored_content = false;
  }
  return options;
}

ebbackup::RestorePathRemap RemapFromC(const EbRestoreRemap* in) {
  ebbackup::RestorePathRemap out{};
  if (!in) return out;
  switch (in->mode) {
    case 1:
      out.mode = ebbackup::RestoreLayoutMode::kStripPrefix;
      break;
    case 2:
      out.mode = ebbackup::RestoreLayoutMode::kFlatten;
      break;
    case 3:
      out.mode = ebbackup::RestoreLayoutMode::kRemapPrefix;
      break;
    default:
      out.mode = ebbackup::RestoreLayoutMode::kKeep;
      break;
  }
  out.strip_prefix = in->strip_prefix ? in->strip_prefix : "";
  out.map_from = in->map_from ? in->map_from : "";
  out.map_to = in->map_to ? in->map_to : "";
  switch (in->conflict) {
    case 1:
      out.conflict = ebbackup::RestoreConflictPolicy::kSkip;
      break;
    case 2:
      out.conflict = ebbackup::RestoreConflictPolicy::kSuffix;
      break;
    default:
      out.conflict = ebbackup::RestoreConflictPolicy::kFail;
      break;
  }
  return out;
}

void ForwardProgress(uint64_t pct, void* user_data) {
  auto* impl = static_cast<EbBackupEngineImpl*>(user_data);
  if (impl && impl->progress_fn) {
    impl->progress_fn(pct, impl->progress_user);
  }
}

void ApplyVssFlags(uint32_t flags, EbBackupEngineImpl* impl,
                   ebbackup::BackupOptions* options) {
  if (!options) return;
  options->use_vss = (flags & EB_BACKUP_FLAG_VSS) != 0;
  if (!options->use_vss) return;
  if (flags & EB_BACKUP_FLAG_VSS_APP) {
    options->vss_mode = ebbackup::winmeta::VssConsistencyMode::kApp;
  } else if (impl && impl->vss_mode_override_set) {
    options->vss_mode = impl->vss_mode_override;
  } else {
    options->vss_mode = ebbackup::winmeta::VssConsistencyMode::kCrash;
  }
  if (impl && impl->vss_include_junction_override_set) {
    options->vss_include_junction_volumes = impl->vss_include_junction_volumes;
  }
  if (impl && impl->vss_fallback_live_set) {
    options->vss_fallback_live = impl->vss_fallback_live;
  }
}

EbStatus RunWithMode(EbBackupEngine* eng, const char* source_path,
                     ebbackup::BackupMode mode, uint32_t flags) {
  if (!eng || !source_path) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  impl->engine->SetProgressCallback(ForwardProgress, impl);
  ebbackup::BackupOptions options{};
  options.use_lz4 = (flags & EB_BACKUP_FLAG_LZ4) != 0;
  options.use_pipeline = (flags & EB_BACKUP_FLAG_PIPELINE) != 0;
  options.disable_pipeline = (flags & EB_BACKUP_FLAG_NO_PIPELINE) != 0;
  options.use_encryption = (flags & EB_BACKUP_FLAG_ENCRYPT) != 0;
  if (flags & EB_BACKUP_FLAG_COMPRESS_AUTO) {
    options.compress_mode = ebbackup::CompressMode::kAuto;
    options.cpu_budget_permille = 600;
  } else if (flags & EB_BACKUP_FLAG_COMPRESS_ZSTD) {
    options.compress_mode = ebbackup::CompressMode::kZstd;
  }
  if (flags & EB_BACKUP_FLAG_BALANCED_DURABILITY) {
    options.durability = ebbackup::DurabilityMode::kBalanced;
  }
  ApplyVssFlags(flags, impl, &options);
  options.sparse_mode = (flags & EB_BACKUP_FLAG_SPARSE_OFF) != 0
                            ? ebbackup::SparseMode::kOff
                            : ebbackup::SparseMode::kAuto;
  options.efs_export_keys = (flags & EB_BACKUP_FLAG_EFS_EXPORT_KEYS) != 0;
  options.encryption_password = impl->password;
  options.filter = impl->filter;
  options.pre_backup_cmd = impl->pre_backup_cmd;
  options.post_backup_cmd = impl->post_backup_cmd;
  options.plugins = impl->plugins;
  const ebbackup::Status st =
      impl->engine->RunBackup(source_path, mode, options);
  if (!st.ok()) impl->last_error = st.message();
  return ToEbStatus(st);
}

EbBackupEngine* OpenImpl(const char* repo_path, EbStatus* err_out) {
  if (!repo_path) {
    if (err_out) *err_out = EB_ERROR_INVALID_ARGUMENT;
    return nullptr;
  }
  auto* impl = new EbBackupEngineImpl();
  impl->engine = std::make_unique<ebbackup::BackupEngine>(repo_path);
  const ebbackup::Status st = impl->engine->Open();
  if (!st.ok()) {
    impl->last_error = st.message();
    if (err_out) *err_out = ToEbStatus(st);
    delete impl;
    return nullptr;
  }
  if (err_out) *err_out = EB_OK;
  return reinterpret_cast<EbBackupEngine*>(impl);
}

}  // namespace

extern "C" {

EbBackupEngine* eb_backup_open(const char* repo_path) {
  return OpenImpl(repo_path, nullptr);
}

EbBackupEngine* eb_backup_open_ex(const char* repo_path, EbStatus* err_out) {
  return OpenImpl(repo_path, err_out);
}

void eb_backup_close(EbBackupEngine* eng) {
  delete reinterpret_cast<EbBackupEngineImpl*>(eng);
}

EbStatus eb_backup_init_repo(const char* repo_path) {
  return eb_backup_init_repo_ex(repo_path, 0);
}

EbStatus eb_backup_init_repo_ex(const char* repo_path, uint32_t flags) {
  if (!repo_path) return EB_ERROR_INVALID_ARGUMENT;
  ebbackup::RepoInitOptions opts{};
  opts.standard_digest = (flags & EB_BACKUP_FLAG_LEGACY_DIGEST) == 0;
  if ((flags & EB_BACKUP_INIT_LEGACY) == 0) {
    opts.persistent_index = true;
    opts.manifest_binary = true;
    opts.snapshots = true;
    opts.ebpack = true;
    opts.coalesced_meta = true;
  }
  return ToEbStatus(ebbackup::BackupEngine::InitRepoEx(repo_path, opts));
}

EbStatus eb_backup_run(EbBackupEngine* eng, const char* source_path) {
  return RunWithMode(eng, source_path, ebbackup::BackupMode::kFull, 0);
}

EbStatus eb_backup_run_incremental(EbBackupEngine* eng, const char* source_path) {
  return RunWithMode(eng, source_path, ebbackup::BackupMode::kIncremental, 0);
}

EbStatus eb_backup_run_ex(EbBackupEngine* eng, const char* source_path,
                          uint32_t flags) {
  return RunWithMode(eng, source_path, ebbackup::BackupMode::kFull, flags);
}

EbStatus eb_backup_run_incremental_ex(EbBackupEngine* eng,
                                      const char* source_path, uint32_t flags) {
  return RunWithMode(eng, source_path, ebbackup::BackupMode::kIncremental, flags);
}

EbStatus eb_backup_verify(EbBackupEngine* eng) {
  return eb_backup_verify_ex(eng, 0);
}

EbStatus eb_backup_verify_ex(EbBackupEngine* eng, uint32_t flags) {
  if (!eng) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::BackupOptions options{};
  options.require_anchor = (flags & EB_BACKUP_FLAG_REQUIRE_ANCHOR) != 0;
  options.encryption_password = impl->password;
  options.audit_key = impl->audit_key;
  const ebbackup::Status st = impl->engine->Verify(options);
  if (!st.ok()) impl->last_error = st.message();
  return ToEbStatus(st);
}

EbStatus eb_backup_recover(EbBackupEngine* eng) {
  if (!eng) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  const ebbackup::Status st = impl->engine->Recover();
  if (!st.ok()) impl->last_error = st.message();
  return ToEbStatus(st);
}

EbStatus eb_backup_restore(EbBackupEngine* eng, const char* dest_path) {
  return eb_backup_restore_ex(eng, dest_path, 0);
}

EbStatus eb_backup_restore_ex(EbBackupEngine* eng, const char* dest_path,
                              uint32_t flags) {
  if (!eng || !dest_path) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  const ebbackup::RestoreOptions options = BuildRestoreOptions(impl, flags);
  const ebbackup::Status st = impl->engine->Restore(dest_path, options);
  if (!st.ok()) impl->last_error = st.message();
  return ToEbStatus(st);
}

void eb_backup_set_password(EbBackupEngine* eng, const char* password) {
  if (!eng) return;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  impl->password = password ? password : "";
  g_bundle_password = impl->password;
}

EbStatus eb_backup_unwrap_with_recovery_key(EbBackupEngine* eng,
                                            const char* recovery_key) {
  if (!eng || !recovery_key) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  const ebbackup::Status st = impl->engine->UnwrapWithRecoveryKey(recovery_key);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  return EB_OK;
}

EbStatus eb_backup_rotate_password(EbBackupEngine* eng, const char* old_password,
                                   const char* new_password) {
  if (!eng || !old_password || !new_password) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  const ebbackup::Status st =
      impl->engine->RotatePassword(old_password, new_password);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  impl->password = new_password;
  return EB_OK;
}

EbStatus eb_backup_set_vss_mode(EbBackupEngine* eng, const char* mode) {
  if (!eng) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  if (!mode || !mode[0]) {
    impl->vss_mode_override_set = false;
    return EB_OK;
  }
  ebbackup::winmeta::VssConsistencyMode parsed{};
  if (!ebbackup::winmeta::ParseVssConsistencyMode(mode, &parsed)) {
    impl->last_error = "invalid vss mode";
    return EB_ERROR_INVALID_ARGUMENT;
  }
  impl->vss_mode_override = parsed;
  impl->vss_mode_override_set = true;
  return EB_OK;
}

void eb_backup_set_vss_include_junction_volumes(EbBackupEngine* eng, int include) {
  if (!eng) return;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  impl->vss_include_junction_volumes = include != 0;
  impl->vss_include_junction_override_set = true;
}

void eb_backup_set_vss_fallback_live(EbBackupEngine* eng, int enable) {
  if (!eng) return;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  impl->vss_fallback_live = enable != 0;
  impl->vss_fallback_live_set = true;
}

void eb_backup_set_audit_key(EbBackupEngine* eng, const char* audit_key) {
  if (!eng) return;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  impl->audit_key = audit_key ? audit_key : "";
  impl->engine->SetAuditKey(impl->audit_key);
}

void eb_backup_set_backup_hooks(EbBackupEngine* eng, const char* pre_cmd,
                                const char* post_cmd) {
  if (!eng) return;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  impl->pre_backup_cmd = pre_cmd ? pre_cmd : "";
  impl->post_backup_cmd = post_cmd ? post_cmd : "";
}

EbStatus eb_backup_set_plugins_json(EbBackupEngine* eng, const char* json) {
  if (!eng) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  impl->plugins.clear();
  if (!json || !json[0]) return EB_OK;
  const ebbackup::Status st =
      ebbackup::ReadJsonStringArrayField(json, "plugins", &impl->plugins);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  return EB_OK;
}

EbStatus eb_backup_load_filter_file(EbBackupEngine* eng, const char* path) {
  if (!eng || !path) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::BackupFilterOptions filter{};
  const ebbackup::Status st = ebbackup::LoadBackupFilterFromFile(path, &filter);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  impl->filter = std::move(filter);
  return EB_OK;
}

EbStatus eb_backup_set_filter_json(EbBackupEngine* eng, const char* json) {
  if (!eng || !json) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::BackupFilterOptions filter{};
  const ebbackup::Status st = ebbackup::ParseBackupFilterJson(json, &filter);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  impl->filter = std::move(filter);
  return EB_OK;
}

EbStatus eb_backup_set_restore_remap(EbBackupEngine* eng,
                                       const EbRestoreRemap* remap) {
  if (!eng) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  impl->remap = RemapFromC(remap);
  return EB_OK;
}

EbStatus eb_backup_set_filter_json_and_remap(EbBackupEngine* eng,
                                             const char* filter_json,
                                             const char* remap_json) {
  if (!eng) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  if (filter_json && filter_json[0] != '\0') {
    ebbackup::BackupFilterOptions filter{};
    const ebbackup::Status st =
        ebbackup::ParseBackupFilterJson(filter_json, &filter);
    if (!st.ok()) {
      impl->last_error = st.message();
      return ToEbStatus(st);
    }
    impl->filter = std::move(filter);
  }
  if (remap_json && remap_json[0] != '\0') {
    ebbackup::RestorePathRemap remap{};
    const ebbackup::Status st =
        ebbackup::ParseRestoreRemapJson(remap_json, &remap);
    if (!st.ok()) {
      impl->last_error = st.message();
      return ToEbStatus(st);
    }
    impl->remap = std::move(remap);
    ebbackup::winmeta::AclRestorePolicy acl{};
    const ebbackup::Status acl_st =
        ebbackup::ParseRestoreAclPolicyJson(remap_json, &acl);
    if (!acl_st.ok()) {
      impl->last_error = acl_st.message();
      return ToEbStatus(acl_st);
    }
    impl->acl_policy = acl;
    ebbackup::winmeta::ReparseRestorePolicy reparse{};
    const ebbackup::Status reparse_st =
        ebbackup::ParseRestoreReparsePolicyJson(remap_json, &reparse);
    if (!reparse_st.ok()) {
      impl->last_error = reparse_st.message();
      return ToEbStatus(reparse_st);
    }
    impl->reparse_policy = reparse;
    ebbackup::SymlinkRemap symlink{};
    const ebbackup::Status symlink_st =
        ebbackup::ParseSymlinkRemapJson(remap_json, &symlink);
    if (!symlink_st.ok()) {
      impl->last_error = symlink_st.message();
      return ToEbStatus(symlink_st);
    }
    impl->symlink_remap = std::move(symlink);
  }
  return EB_OK;
}

EbStatus eb_backup_preview_restore_at(EbBackupEngine* eng, uint64_t txn_id,
                                      EbRestorePreviewReport* report) {
  if (!eng || !report) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::RestorePreviewReport local{};
  const ebbackup::RestoreOptions options = BuildRestoreOptions(impl, 0, txn_id);
  const ebbackup::Status st =
      impl->engine->PreviewRestore(txn_id, options, &local);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  report->file_count = local.file_count;
  report->dir_count = local.dir_count;
  report->total_bytes = local.total_bytes;
  return EB_OK;
}

EbStatus eb_backup_gc_orphans(EbBackupEngine* eng, int dry_run) {
  return eb_backup_gc_orphans_ex(eng, dry_run, nullptr);
}

EbStatus eb_backup_gc_orphans_ex(EbBackupEngine* eng, int dry_run,
                                 EbOrphanGcReport* report) {
  if (!eng) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::OrphanGcReport local{};
  const ebbackup::Status st =
      impl->engine->GcOrphans(dry_run != 0, report ? &local : nullptr);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  if (report) {
    report->referenced_count = local.referenced_count;
    report->orphan_count = local.orphan_count;
    report->tombstoned_count = local.tombstoned_count;
  }
  return EB_OK;
}

EbStatus eb_backup_get_stats(EbBackupEngine* eng, EbBackupStats* out) {
  if (!eng || !out) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  const auto& s = impl->engine->stats();
  out->files_processed = s.files_processed;
  out->chunks_written = s.chunks_written;
  out->chunks_reused = s.chunks_reused;
  out->chunks_reused_from_cfi = s.chunks_reused_from_cfi;
  out->bytes_processed = s.bytes_processed;
  out->orphan_chunks_hint = s.orphan_chunks_hint;
  out->content_incompressible_skips = s.content_class.incompressible_skips;
  out->content_lz4_only = s.content_class.lz4_only;
  out->content_zstd_attempts = s.content_class.zstd_attempts;
  out->content_zstd_wins = s.content_class.zstd_wins;
  return EB_OK;
}

EbStatus eb_backup_compact(EbBackupEngine* eng, int dry_run,
                           EbCompactReport* report) {
  if (!eng) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::CompactReport local{};
  const ebbackup::Status st =
      impl->engine->Compact(dry_run != 0, report ? &local : nullptr);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  if (report) {
    report->physical_before = local.physical_before;
    report->physical_after = local.physical_after;
    report->live_bytes = local.live_bytes;
    report->records_copied = local.records_copied;
    report->ampl_ratio_before = local.ampl_ratio_before;
    report->ampl_ratio_after = local.ampl_ratio_after;
  }
  return EB_OK;
}

EbStatus eb_backup_repo_stats(EbBackupEngine* eng, EbRepoStats* out) {
  if (!eng || !out) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::RepoStats stats{};
  const ebbackup::Status st = impl->engine->GetRepoStats(&stats);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  out->physical_bytes = stats.physical_bytes;
  out->live_bytes = stats.live_bytes;
  out->orphan_bytes = stats.orphan_bytes;
  out->manifest_bytes = stats.manifest_bytes;
  out->unique_chunks = stats.unique_chunks;
  out->tombstoned_chunks = stats.tombstoned_chunks;
  out->ampl_ratio = stats.ampl_ratio;
  out->live_uncompressed_bytes = stats.live_uncompressed_bytes;
  out->live_stored_payload_bytes = stats.live_stored_payload_bytes;
  out->compress_ratio = stats.compress_ratio;
  out->compressed_chunk_count = stats.compressed_chunk_count;
  out->raw_chunk_count = stats.raw_chunk_count;
  out->has_zstd_dict = stats.has_zstd_dict ? 1u : 0u;
  out->zstd_dict_bytes = stats.zstd_dict_bytes;
  return EB_OK;
}

void eb_backup_set_progress(EbBackupEngine* eng, EbProgressFn fn, void* user_data) {
  if (!eng) return;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  impl->progress_fn = fn;
  impl->progress_user = user_data;
}

uint32_t eb_backup_abi_version(void) { return EB_BACKUP_ABI_VERSION; }

char* eb_backup_last_error(EbBackupEngine* eng) {
  if (!eng) return nullptr;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  if (impl->last_error.empty()) return nullptr;
  char* out = static_cast<char*>(std::malloc(impl->last_error.size() + 1));
  if (!out) return nullptr;
  std::memcpy(out, impl->last_error.c_str(), impl->last_error.size() + 1);
  return out;
}

void eb_backup_free_string(char* s) { std::free(s); }

EbStatus eb_backup_verify_at(EbBackupEngine* eng, uint64_t txn_id) {
  if (!eng) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::BackupOptions opts{};
  opts.snapshot_txn_id = txn_id;
  opts.audit_key = impl->audit_key;
  const ebbackup::Status st = impl->engine->Verify(opts);
  if (!st.ok()) impl->last_error = st.message();
  return ToEbStatus(st);
}

EbStatus eb_backup_restore_at(EbBackupEngine* eng, const char* dest_path,
                              uint64_t txn_id, uint32_t flags) {
  if (!eng || !dest_path) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  const ebbackup::RestoreOptions options =
      BuildRestoreOptions(impl, flags, txn_id);
  const ebbackup::Status st = impl->engine->Restore(dest_path, options);
  if (!st.ok()) impl->last_error = st.message();
  return ToEbStatus(st);
}

EbStatus eb_backup_run_maintenance_wizard(
    EbBackupEngine* eng, const EbMaintenanceWizardOptions* options,
    EbMaintenanceWizardReport* report) {
  if (!eng || !options || !report) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::MaintenanceWizardOptions opts{};
  opts.run_prune = options->run_prune != 0;
  opts.retention_tiers =
      options->retention_tiers ? options->retention_tiers : "";
  opts.retain_min = options->retain_min > 0 ? options->retain_min : 3;
  opts.run_gc = options->run_gc != 0;
  opts.run_compact = options->run_compact != 0;
  opts.dry_run_only = options->dry_run_only != 0;
  opts.verify_after = options->verify_after != 0;
  ebbackup::MaintenanceWizardReport local{};
  const ebbackup::Status st =
      ebbackup::RunMaintenanceWizard(impl->engine.get(), opts, &local);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  report->stats_before.physical_bytes = local.stats_before.physical_bytes;
  report->stats_before.live_bytes = local.stats_before.live_bytes;
  report->stats_before.orphan_bytes = local.stats_before.orphan_bytes;
  report->stats_before.manifest_bytes = local.stats_before.manifest_bytes;
  report->stats_before.unique_chunks = local.stats_before.unique_chunks;
  report->stats_before.tombstoned_chunks = local.stats_before.tombstoned_chunks;
  report->stats_before.ampl_ratio = local.stats_before.ampl_ratio;
  report->stats_after.physical_bytes = local.stats_after.physical_bytes;
  report->stats_after.live_bytes = local.stats_after.live_bytes;
  report->stats_after.orphan_bytes = local.stats_after.orphan_bytes;
  report->stats_after.manifest_bytes = local.stats_after.manifest_bytes;
  report->stats_after.unique_chunks = local.stats_after.unique_chunks;
  report->stats_after.tombstoned_chunks = local.stats_after.tombstoned_chunks;
  report->stats_after.ampl_ratio = local.stats_after.ampl_ratio;
  report->prune.kept_count = local.prune.kept_count;
  report->prune.pruned_count = local.prune.pruned_count;
  report->gc.referenced_count = local.gc.referenced_count;
  report->gc.orphan_count = local.gc.orphan_count;
  report->gc.tombstoned_count = local.gc.tombstoned_count;
  report->compact.physical_before = local.compact.physical_before;
  report->compact.physical_after = local.compact.physical_after;
  report->compact.live_bytes = local.compact.live_bytes;
  report->compact.records_copied = local.compact.records_copied;
  report->compact.ampl_ratio_before = local.compact.ampl_ratio_before;
  report->compact.ampl_ratio_after = local.compact.ampl_ratio_after;
  report->compact_skipped = local.compact_skipped ? 1 : 0;
  report->verify_ran = local.verify_ran ? 1 : 0;
  report->verify_ok = local.verify_ok ? 1 : 0;
  return EB_OK;
}

EbStatus eb_backup_list_snapshots(EbBackupEngine* eng, EbSnapshotInfo** out,
                                  size_t* count) {
  if (!eng || !out || !count) return EB_ERROR_INVALID_ARGUMENT;
  *out = nullptr;
  *count = 0;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  std::vector<ebbackup::SnapshotEntry> entries;
  const ebbackup::Status st = impl->engine->ListSnapshots(&entries);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  if (entries.empty()) return EB_OK;
  auto* arr = static_cast<EbSnapshotInfo*>(
      std::malloc(entries.size() * sizeof(EbSnapshotInfo)));
  if (!arr) return EB_ERROR_INTERNAL;
  for (size_t i = 0; i < entries.size(); ++i) {
    arr[i].txn_id = entries[i].txn_id;
    arr[i].created_at_unix = entries[i].created_at_unix;
    arr[i].manifest_crc32 = entries[i].manifest_crc32;
    arr[i].file_count = entries[i].file_count;
  }
  *out = arr;
  *count = entries.size();
  return EB_OK;
}

void eb_backup_free_snapshots(EbSnapshotInfo* snapshots) { std::free(snapshots); }

EbStatus eb_backup_list_manifest_files(EbBackupEngine* eng, uint64_t txn_id,
                                       uint64_t* manifest_txn_id,
                                       EbManifestFileInfo** out, size_t* count) {
  if (!eng || !out || !count) return EB_ERROR_INVALID_ARGUMENT;
  *out = nullptr;
  *count = 0;
  if (manifest_txn_id) *manifest_txn_id = 0;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::ManifestDocument doc;
  const ebbackup::Status st = impl->engine->LoadManifest(txn_id, &doc);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  if (manifest_txn_id) *manifest_txn_id = doc.txn_id;
  if (doc.files.empty()) return EB_OK;
  auto* arr = static_cast<EbManifestFileInfo*>(
      std::malloc(doc.files.size() * sizeof(EbManifestFileInfo)));
  if (!arr) return EB_ERROR_INTERNAL;
  for (size_t i = 0; i < doc.files.size(); ++i) {
    const auto& f = doc.files[i];
    char* path = static_cast<char*>(std::malloc(f.relative_path.size() + 1));
    if (!path) {
      for (size_t j = 0; j < i; ++j) std::free(arr[j].relative_path);
      std::free(arr);
      return EB_ERROR_INTERNAL;
    }
    std::memcpy(path, f.relative_path.c_str(), f.relative_path.size() + 1);
    arr[i].relative_path = path;
    arr[i].size = f.size;
    arr[i].file_type = static_cast<uint8_t>(f.file_type);
    arr[i].mtime_unix = f.mtime_unix;
    arr[i].chunk_count =
        static_cast<uint32_t>(f.chunk_hashes_hex.size());
  }
  *out = arr;
  *count = doc.files.size();
  return EB_OK;
}

void eb_backup_free_manifest_files(EbManifestFileInfo* files, size_t count) {
  if (!files) return;
  for (size_t i = 0; i < count; ++i) {
    std::free(files[i].relative_path);
  }
  std::free(files);
}

EbStatus eb_backup_prune_snapshots(EbBackupEngine* eng,
                                   const char* retention_tiers, int retain_min,
                                   int dry_run, EbPruneReport* report) {
  if (!eng) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::RetentionPolicy policy = ebbackup::DefaultRetentionPolicy();
  if (retention_tiers && retention_tiers[0] != '\0') {
    const ebbackup::Status parse_st =
        ebbackup::ParseRetentionTiers(retention_tiers, &policy);
    if (!parse_st.ok()) {
      impl->last_error = parse_st.message();
      return ToEbStatus(parse_st);
    }
  }
  if (retain_min > 0) policy.retain_min = retain_min;
  ebbackup::PruneReport local{};
  const ebbackup::Status st =
      impl->engine->PruneSnapshots(policy, dry_run != 0, &local, impl->audit_key);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  if (report) {
    report->kept_count = local.kept_count;
    report->pruned_count = local.pruned_count;
  }
  return EB_OK;
}

namespace {

char* AllocJsonString(const std::string& s) {
  char* out = static_cast<char*>(std::malloc(s.size() + 1));
  if (!out) return nullptr;
  std::memcpy(out, s.c_str(), s.size() + 1);
  return out;
}

}  // namespace

EbStatus eb_backup_build_path_index(EbBackupEngine* eng, int full_rebuild) {
  if (!eng) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  const ebbackup::Status st = impl->engine->BuildPathIndex(full_rebuild != 0);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  return EB_OK;
}

char* eb_backup_query_path_history_json(EbBackupEngine* eng, const char* path,
                                        uint64_t offset, uint64_t limit) {
  if (!eng || !path) return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  if (limit == 0) limit = 100;
  return AllocJsonString(impl->engine->QueryPathHistoryJson(path, offset, limit));
}

char* eb_backup_list_manifest_files_page_json(EbBackupEngine* eng, uint64_t txn_id,
                                              const char* prefix, uint64_t offset,
                                              uint64_t limit) {
  if (!eng) return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  const std::string pfx = prefix ? prefix : "";
  if (limit == 0) limit = 100;
  return AllocJsonString(
      impl->engine->ListManifestFilesPageJson(txn_id, pfx, offset, limit));
}

char* eb_backup_diff_snapshots_json(EbBackupEngine* eng, uint64_t txn_a,
                                    uint64_t txn_b) {
  if (!eng) return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  return AllocJsonString(impl->engine->DiffSnapshotsJson(txn_a, txn_b));
}

char* eb_backup_export_restore_report_json(EbBackupEngine* eng) {
  if (!eng) return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  return AllocJsonString(impl->engine->ExportRestoreReportJson());
}

char* eb_backup_get_backup_report_json(EbBackupEngine* eng, uint64_t txn_id) {
  if (!eng) return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  return AllocJsonString(impl->engine->ExportBackupReportJson(txn_id));
}

char* eb_backup_preview_in_place_json(EbBackupEngine* eng, uint64_t txn_id,
                                      const char* target_root,
                                      const char* options_json) {
  if (!eng || !target_root) {
    return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  }
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::restore::InPlacePreviewOptions preview_opts{};
  if (options_json && options_json[0]) {
    const std::string json = options_json;
    const auto base_pos = json.find("\"base_txn_id\"");
    if (base_pos != std::string::npos) {
      size_t i = base_pos;
      while (i < json.size() && json[i] != ':') ++i;
      if (i < json.size()) ++i;
      while (i < json.size() && !std::isdigit(static_cast<unsigned char>(json[i]))) ++i;
      size_t start = i;
      while (i < json.size() && std::isdigit(static_cast<unsigned char>(json[i]))) ++i;
      if (start < i) {
        preview_opts.base_txn_id = std::strtoull(json.substr(start, i - start).c_str(),
                                                 nullptr, 10);
      }
    }
    if (json.find("\"use_three_way\":false") != std::string::npos ||
        json.find("\"use_three_way\": false") != std::string::npos) {
      preview_opts.use_three_way = false;
    }
  }
  ebbackup::restore::InPlacePreviewReport report{};
  const ebbackup::RestoreOptions options = BuildRestoreOptions(impl, 0, txn_id);
  const ebbackup::Status st = impl->engine->PreviewInPlaceRestore(
      txn_id, target_root, options, preview_opts, &report);
  if (!st.ok()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"" + st.message() + "\"}");
  }
  return AllocJsonString(ebbackup::restore::InPlacePreviewReportToJson(report));
}

char* eb_backup_apply_in_place_json(EbBackupEngine* eng, uint64_t txn_id,
                                    const char* target_root,
                                    const char* options_json) {
  if (!eng || !target_root) {
    return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  }
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::restore::InPlacePreviewOptions preview_opts{};
  ebbackup::restore::InPlaceApplyOptions apply_opts{};
  if (options_json && options_json[0]) {
    const std::string json = options_json;
    const auto base_pos = json.find("\"base_txn_id\"");
    if (base_pos != std::string::npos) {
      size_t i = base_pos;
      while (i < json.size() && json[i] != ':') ++i;
      if (i < json.size()) ++i;
      while (i < json.size() && !std::isdigit(static_cast<unsigned char>(json[i]))) ++i;
      size_t start = i;
      while (i < json.size() && std::isdigit(static_cast<unsigned char>(json[i]))) ++i;
      if (start < i) {
        preview_opts.base_txn_id = std::strtoull(json.substr(start, i - start).c_str(),
                                                 nullptr, 10);
      }
    }
    if (json.find("\"use_three_way\":false") != std::string::npos ||
        json.find("\"use_three_way\": false") != std::string::npos) {
      preview_opts.use_three_way = false;
    }
    if (json.find("\"dry_run\":true") != std::string::npos ||
        json.find("\"dry_run\": true") != std::string::npos) {
      apply_opts.dry_run = true;
    }
    if (json.find("\"conflict_policy\":\"skip\"") != std::string::npos ||
        json.find("\"conflict_policy\": \"skip\"") != std::string::npos) {
      apply_opts.conflict = ebbackup::restore::InPlaceConflictPolicy::kSkip;
    } else if (json.find("\"conflict_policy\":\"fail\"") != std::string::npos ||
               json.find("\"conflict_policy\": \"fail\"") != std::string::npos) {
      apply_opts.conflict = ebbackup::restore::InPlaceConflictPolicy::kFail;
    } else if (json.find("\"conflict_policy\":\"overwrite\"") != std::string::npos ||
               json.find("\"conflict_policy\": \"overwrite\"") != std::string::npos) {
      apply_opts.conflict = ebbackup::restore::InPlaceConflictPolicy::kOverwrite;
    }
    if (json.find("\"orphan_policy\":\"delete\"") != std::string::npos ||
        json.find("\"orphan_policy\": \"delete\"") != std::string::npos) {
      apply_opts.orphan = ebbackup::restore::InPlaceOrphanPolicy::kDelete;
    }
  }
  ebbackup::restore::InPlaceApplyReport report{};
  const ebbackup::RestoreOptions options = BuildRestoreOptions(impl, 0, txn_id);
  const ebbackup::Status st = impl->engine->ApplyInPlaceRestore(
      txn_id, target_root, options, preview_opts, apply_opts, &report);
  if (!st.ok()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"" + st.message() + "\"}");
  }
  return AllocJsonString(ebbackup::restore::InPlaceApplyReportToJson(report));
}

char* eb_backup_export_delta_json(const char* repo_path, const char* bundle_path,
                                  uint64_t base_txn_id, uint64_t target_txn_id,
                                  uint32_t flags) {
  if (!repo_path || !bundle_path || base_txn_id == 0) {
    return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  }
  ebbackup::EbBundleDeltaOptions opts{};
  opts.base_txn_id = base_txn_id;
  opts.target_txn_id = target_txn_id;
  opts.encrypt_bundle = (flags & EB_BACKUP_FLAG_ENCRYPT) != 0;
  opts.password = g_bundle_password;
  ebbackup::EbBundleDeltaStats stats{};
  const ebbackup::Status st =
      ebbackup::ExportRepoDeltaToBundle(repo_path, bundle_path, opts, &stats);
  if (!st.ok()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"" + st.message() + "\"}");
  }
  std::ostringstream out;
  out << "{\"ok\":true";
  out << ",\"base_txn_id\":" << stats.base_txn_id;
  out << ",\"target_txn_id\":" << stats.target_txn_id;
  out << ",\"chunk_count\":" << stats.delta_chunk_count;
  out << ",\"bytes\":" << stats.delta_bytes;
  out << ",\"reuse_ratio\":" << stats.reuse_ratio;
  out << '}';
  return AllocJsonString(out.str());
}

char* eb_backup_import_delta_json(const char* base_path, const char* delta_path,
                                  const char* out_repo_path) {
  if (!base_path || !delta_path || !out_repo_path) {
    return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  }
  const ebbackup::Status st =
      ebbackup::ImportBundleDeltaToRepo(base_path, delta_path, out_repo_path);
  if (!st.ok()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"" + st.message() + "\"}");
  }
  return AllocJsonString("{\"ok\":true}");
}

char* eb_backup_apply_delta_json(const char* delta_path, const char* repo_path) {
  if (!delta_path || !repo_path) {
    return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  }
  const ebbackup::Status st =
      ebbackup::ApplyDeltaBundleToRepo(delta_path, repo_path);
  if (!st.ok()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"" + st.message() + "\"}");
  }
  return AllocJsonString("{\"ok\":true}");
}

char* eb_backup_list_jobs_json(const char* repo_path) {
  if (!repo_path) return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  std::vector<ebbackup::job::BackupJob> jobs;
  const ebbackup::Status st = ebbackup::job::LoadJobs(repo_path, &jobs);
  if (!st.ok()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"" + st.message() + "\"}");
  }
  return AllocJsonString("{\"ok\":true,\"jobs\":" +
                         ebbackup::job::JobsToJson(jobs) + "}");
}

EbStatus eb_backup_upsert_job_json(const char* repo_path, const char* job_json) {
  if (!repo_path || !job_json) return EB_ERROR_INVALID_ARGUMENT;
  ebbackup::job::BackupJob job{};
  const ebbackup::Status parse_st = ebbackup::job::ParseJobJson(job_json, &job);
  if (!parse_st.ok()) return ToEbStatus(parse_st);
  return ToEbStatus(ebbackup::job::UpsertJob(repo_path, job));
}

EbStatus eb_backup_delete_job(const char* repo_path, const char* job_id) {
  if (!repo_path || !job_id) return EB_ERROR_INVALID_ARGUMENT;
  return ToEbStatus(ebbackup::job::DeleteJob(repo_path, job_id));
}

EbStatus eb_backup_run_job(EbBackupEngine* eng, const char* job_id, int incremental,
                           uint32_t flags) {
  if (!eng || !job_id) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  impl->engine->SetProgressCallback(ForwardProgress, impl);
  ebbackup::BackupOptions options{};
  options.use_lz4 = (flags & EB_BACKUP_FLAG_LZ4) != 0;
  options.use_pipeline = (flags & EB_BACKUP_FLAG_PIPELINE) != 0;
  options.disable_pipeline = (flags & EB_BACKUP_FLAG_NO_PIPELINE) != 0;
  options.use_encryption = (flags & EB_BACKUP_FLAG_ENCRYPT) != 0;
  if (flags & EB_BACKUP_FLAG_COMPRESS_AUTO) {
    options.compress_mode = ebbackup::CompressMode::kAuto;
    options.cpu_budget_permille = 600;
  } else if (flags & EB_BACKUP_FLAG_COMPRESS_ZSTD) {
    options.compress_mode = ebbackup::CompressMode::kZstd;
  }
  if (flags & EB_BACKUP_FLAG_BALANCED_DURABILITY) {
    options.durability = ebbackup::DurabilityMode::kBalanced;
  }
  ApplyVssFlags(flags, impl, &options);
  options.encryption_password = impl->password;
  options.filter = impl->filter;
  options.pre_backup_cmd = impl->pre_backup_cmd;
  options.post_backup_cmd = impl->post_backup_cmd;
  options.plugins = impl->plugins;
  const auto mode = incremental ? ebbackup::BackupMode::kIncremental
                                : ebbackup::BackupMode::kFull;
  const ebbackup::Status st = impl->engine->RunJob(job_id, mode, options);
  if (!st.ok()) {
    impl->last_error = st.message();
    return ToEbStatus(st);
  }
  return EB_OK;
}

char* eb_backup_enqueue_job_json(const char* repo_path, const char* job_json) {
  if (!repo_path || !job_json) {
    return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  }
  std::string job_id;
  bool incremental = false;
  uint32_t flags = 0;
  const std::string json = job_json;
  size_t i = 0;
  while (i < json.size()) {
    while (i < json.size() && json[i] != '"') ++i;
    if (i >= json.size()) break;
    ++i;
    const size_t key_start = i;
    while (i < json.size() && json[i] != '"') ++i;
    const std::string key = json.substr(key_start, i - key_start);
    ++i;
    while (i < json.size() && json[i] != ':' && json[i] != '"') ++i;
    if (i < json.size() && json[i] == ':') ++i;
    if (key == "job_id") {
      while (i < json.size() && json[i] != '"') ++i;
      if (i < json.size()) ++i;
      const size_t val_start = i;
      while (i < json.size() && json[i] != '"') ++i;
      job_id = json.substr(val_start, i - val_start);
    } else if (key == "incremental") {
      incremental = json.find("true", i) != std::string::npos &&
                    json.find("true", i) < json.find(',', i);
    } else if (key == "flags") {
      while (i < json.size() && !std::isdigit(static_cast<unsigned char>(json[i]))) {
        ++i;
      }
      const size_t val_start = i;
      while (i < json.size() && std::isdigit(static_cast<unsigned char>(json[i]))) ++i;
      if (val_start < i) {
        flags = static_cast<uint32_t>(
            std::strtoul(json.substr(val_start, i - val_start).c_str(), nullptr, 10));
      }
    }
  }
  if (job_id.empty()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"job_id required\"}");
  }
  ebbackup::BackupEngine engine(repo_path);
  const ebbackup::Status open_st = engine.Open();
  if (!open_st.ok()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"" + open_st.message() + "\"}");
  }
  const ebbackup::Status st = engine.EnqueueJob(job_id, incremental, flags);
  if (!st.ok()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"" + st.message() + "\"}");
  }
  return AllocJsonString("{\"ok\":true,\"job_id\":\"" + job_id + "\"}");
}

char* eb_backup_run_job_queue_json(EbBackupEngine* eng, const char* options_json) {
  if (!eng) return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  bool drain = false;
  if (options_json) {
    const std::string json = options_json;
    drain = json.find("\"drain\":true") != std::string::npos ||
            json.find("\"drain\": true") != std::string::npos;
  }
  ebbackup::BackupOptions options{};
  options.encryption_password = impl->password;
  options.filter = impl->filter;
  options.pre_backup_cmd = impl->pre_backup_cmd;
  options.post_backup_cmd = impl->post_backup_cmd;
  options.plugins = impl->plugins;
  const ebbackup::Status st = impl->engine->RunJobQueue(drain, options);
  if (!st.ok()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"" + st.message() + "\"}");
  }
  return AllocJsonString("{\"ok\":true,\"drain\":" + std::string(drain ? "true" : "false") +
                         "}");
}

char* eb_backup_job_queue_status_json(const char* repo_path) {
  if (!repo_path) {
    return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  }
  ebbackup::BackupEngine engine(repo_path);
  const ebbackup::Status open_st = engine.Open();
  if (!open_st.ok()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"" + open_st.message() + "\"}");
  }
  return AllocJsonString(engine.JobQueueStatusJson());
}

char* eb_backup_list_job_reports_json(const char* repo_path, const char* job_id,
                                      uint64_t offset, uint64_t limit) {
  if (!repo_path || !job_id) {
    return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  }
  if (limit == 0) limit = 100;
  std::vector<ebbackup::catalog::JobReportLine> all;
  const ebbackup::Status load_st =
      ebbackup::catalog::ListJobReports(repo_path, job_id, 0, UINT64_MAX, &all);
  if (!load_st.ok()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"" + load_st.message() + "\"}");
  }
  const uint64_t total = static_cast<uint64_t>(all.size());
  std::vector<ebbackup::catalog::JobReportLine> page;
  if (offset < all.size()) {
    const size_t end =
        std::min(all.size(), static_cast<size_t>(offset + limit));
    page.assign(all.begin() + static_cast<size_t>(offset), all.begin() + end);
  }
  return AllocJsonString(
      ebbackup::catalog::JobReportsToJson(page, total, offset));
}

char* eb_backup_snapshot_reachability_json(EbBackupEngine* eng, uint64_t txn_id) {
  if (!eng) return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  return AllocJsonString(impl->engine->SnapshotReachabilityJson(txn_id));
}

char* eb_backup_rpo_summary_json(EbBackupEngine* eng) {
  if (!eng) return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  return AllocJsonString(impl->engine->RpoSummaryJson());
}

char* eb_backup_orphan_explain_json(EbBackupEngine* eng, uint64_t sample_limit) {
  if (!eng) return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  return AllocJsonString(impl->engine->OrphanExplainJson(sample_limit));
}

char* eb_backup_append_ops_audit_json(EbBackupEngine* eng, const char* op_json) {
  if (!eng || !op_json) {
    return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  }
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  return AllocJsonString(impl->engine->AppendOpsAuditJson(op_json));
}

char* eb_backup_list_ops_audit_json(EbBackupEngine* eng) {
  if (!eng) return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  return AllocJsonString(impl->engine->ListOpsAuditJson());
}

char* eb_backup_suggest_exclude_filters_json(const char* options_json) {
  if (!options_json) {
    return AllocJsonString("{\"ok\":false,\"error\":\"invalid_argument\"}");
  }
  std::string source;
  if (!ebbackup::ReadJsonStringField(options_json, "source_path", &source).ok() ||
      source.empty()) {
    return AllocJsonString("{\"ok\":false,\"error\":\"source_path required\"}");
  }
  int max_depth = 4;
  (void)ebbackup::ReadJsonIntField(options_json, "max_depth", &max_depth);
  bool include_ide_dirs = false;
  (void)ebbackup::ReadJsonBoolField(options_json, "include_ide_dirs", &include_ide_dirs);

  ebbackup::BackupFilterOptions existing{};
  ebbackup::BackupFilterOptions* existing_ptr = nullptr;
  const std::string json = options_json;
  const size_t ex_pos = json.find("\"existing\"");
  if (ex_pos != std::string::npos) {
    const size_t brace = json.find('{', ex_pos);
    if (brace != std::string::npos) {
      int depth = 0;
      size_t end = brace;
      for (; end < json.size(); ++end) {
        if (json[end] == '{') ++depth;
        else if (json[end] == '}') {
          --depth;
          if (depth == 0) {
            ++end;
            break;
          }
        }
      }
      const std::string obj = json.substr(brace, end - brace);
      if (ebbackup::ParseBackupFilterJson(obj, &existing).ok()) {
        existing_ptr = &existing;
      }
    }
  }

  ebbackup::SuggestExcludeFiltersOptions opts{};
  opts.max_depth = max_depth > 0 ? max_depth : 4;
  opts.include_ide_dirs = include_ide_dirs;
  opts.existing = existing_ptr;
  ebbackup::ExcludeFilterSuggestions suggestions{};
  const ebbackup::Status st =
      ebbackup::SuggestExcludeFilters(source, opts, &suggestions);
  if (!st.ok()) {
    std::ostringstream err;
    err << "{\"ok\":false,\"error\":\"" << st.message() << "\"}";
    return AllocJsonString(err.str());
  }
  const std::string body = ebbackup::ExcludeFilterSuggestionsToJson(suggestions);
  if (body.empty() || body.front() != '{') {
    return AllocJsonString("{\"ok\":false,\"error\":\"encode failed\"}");
  }
  return AllocJsonString(std::string("{\"ok\":true,") + body.substr(1));
}

const char* eb_backup_engine_repo_path(EbBackupEngine* eng) {
  if (!eng) return "";
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  return impl->engine->repo_path().c_str();
}

}  // extern "C"
