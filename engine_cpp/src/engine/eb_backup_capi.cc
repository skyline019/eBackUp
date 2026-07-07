#include "ebbackup/eb_backup.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/scan/backup_filter.h"

namespace {

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
  ebbackup::BackupFilterOptions filter;
  EbProgressFn progress_fn{nullptr};
  void* progress_user{nullptr};
};

void ForwardProgress(uint64_t pct, void* user_data) {
  auto* impl = static_cast<EbBackupEngineImpl*>(user_data);
  if (impl && impl->progress_fn) {
    impl->progress_fn(pct, impl->progress_user);
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
  options.encryption_password = impl->password;
  options.filter = impl->filter;
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
  ebbackup::RestoreOptions options{};
  options.encryption_password = impl->password;
  options.filter = impl->filter;
  if (flags & EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY) {
    options.verify_subset_merkle = false;
    options.verify_restored_content = false;
  }
  const ebbackup::Status st = impl->engine->Restore(dest_path, options);
  if (!st.ok()) impl->last_error = st.message();
  return ToEbStatus(st);
}

void eb_backup_set_password(EbBackupEngine* eng, const char* password) {
  if (!eng) return;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  impl->password = password ? password : "";
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

EbStatus eb_backup_gc_orphans(EbBackupEngine* eng, int dry_run) {
  if (!eng) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  const ebbackup::Status st = impl->engine->GcOrphans(dry_run != 0, nullptr);
  if (!st.ok()) impl->last_error = st.message();
  return ToEbStatus(st);
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
  const ebbackup::Status st = impl->engine->Verify(opts);
  if (!st.ok()) impl->last_error = st.message();
  return ToEbStatus(st);
}

EbStatus eb_backup_restore_at(EbBackupEngine* eng, const char* dest_path,
                              uint64_t txn_id, uint32_t flags) {
  if (!eng || !dest_path) return EB_ERROR_INVALID_ARGUMENT;
  auto* impl = reinterpret_cast<EbBackupEngineImpl*>(eng);
  ebbackup::RestoreOptions opts{};
  opts.encryption_password = impl->password;
  opts.filter = impl->filter;
  opts.snapshot_txn_id = txn_id;
  if (flags & EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY) {
    opts.verify_subset_merkle = false;
    opts.verify_restored_content = false;
  }
  const ebbackup::Status st = impl->engine->Restore(dest_path, opts);
  if (!st.ok()) impl->last_error = st.message();
  return ToEbStatus(st);
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
      impl->engine->PruneSnapshots(policy, dry_run != 0, &local);
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

}  // extern "C"
