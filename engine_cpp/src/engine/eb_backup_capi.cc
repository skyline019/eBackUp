#include "ebbackup/eb_backup.h"

#include <cstring>
#include <memory>
#include <string>

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
  options.use_encryption = (flags & EB_BACKUP_FLAG_ENCRYPT) != 0;
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
  if (!repo_path) return EB_ERROR_INVALID_ARGUMENT;
  return ToEbStatus(ebbackup::BackupEngine::InitRepo(repo_path));
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

}  // extern "C"
