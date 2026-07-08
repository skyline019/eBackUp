#include "ebbackup/ebbackup_workbench.h"

#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebbackup/catalog/snapshot_meta.h"
#include "ebbackup/engine/restore_options_json.h"
#include "ebbackup/store/maintenance_wizard.h"

namespace {

int CopyOut(const std::string& s, char* out, size_t cap) {
  if (!out || cap == 0) return -1;
  if (s.size() + 1 > cap) return -2;
  std::memcpy(out, s.data(), s.size());
  out[s.size()] = '\0';
  return 0;
}

void JsonEscape(const std::string& s, std::string* out) {
  *out += '"';
  for (char c : s) {
    switch (c) {
      case '"':
        *out += "\\\"";
        break;
      case '\\':
        *out += "\\\\";
        break;
      case '\n':
        *out += "\\n";
        break;
      case '\r':
        *out += "\\r";
        break;
      case '\t':
        *out += "\\t";
        break;
      default:
        *out += c;
        break;
    }
  }
  *out += '"';
}

std::string StatusName(EbStatus st) {
  switch (st) {
    case EB_OK:
      return "ok";
    case EB_ERROR_INVALID_ARGUMENT:
      return "invalid_argument";
    case EB_ERROR_NOT_FOUND:
      return "not_found";
    case EB_ERROR_CORRUPTED:
      return "corrupted";
    case EB_ERROR_IO:
      return "io";
    case EB_ERROR_INTERNAL:
      return "internal";
    case EB_ERROR_CONFLICT:
      return "conflict";
    default:
      return "unknown";
  }
}

std::string ErrJson(EbStatus st, const char* msg) {
  std::string j = "{\"ok\":false,\"status\":\"" + StatusName(st) + "\"";
  if (msg && msg[0]) {
    j += ",\"error\":";
    JsonEscape(msg, &j);
  }
  j += '}';
  return j;
}

std::string LastErrJson(EbBackupEngine* eng, EbStatus st) {
  char* err = eb_backup_last_error(eng);
  const std::string j = ErrJson(st, err ? err : "");
  if (err) eb_backup_free_string(err);
  return j;
}

void AppendStatsJson(const EbBackupStats& s, std::string* j) {
  *j += ",\"stats\":{";
  *j += "\"files_processed\":" + std::to_string(s.files_processed);
  *j += ",\"chunks_written\":" + std::to_string(s.chunks_written);
  *j += ",\"chunks_reused\":" + std::to_string(s.chunks_reused);
  *j += ",\"chunks_reused_from_cfi\":" + std::to_string(s.chunks_reused_from_cfi);
  *j += ",\"bytes_processed\":" + std::to_string(s.bytes_processed);
  *j += ",\"orphan_chunks_hint\":" + std::to_string(s.orphan_chunks_hint);
  *j += ",\"content_incompressible_skips\":" + std::to_string(s.content_incompressible_skips);
  *j += ",\"content_lz4_only\":" + std::to_string(s.content_lz4_only);
  *j += ",\"content_zstd_attempts\":" + std::to_string(s.content_zstd_attempts);
  *j += ",\"content_zstd_wins\":" + std::to_string(s.content_zstd_wins);
  *j += '}';
}

void AppendRepoStatsJson(const EbRepoStats& s, std::string* j) {
  *j += ",\"repo_stats\":{";
  *j += "\"physical_bytes\":" + std::to_string(s.physical_bytes);
  *j += ",\"live_bytes\":" + std::to_string(s.live_bytes);
  *j += ",\"orphan_bytes\":" + std::to_string(s.orphan_bytes);
  *j += ",\"manifest_bytes\":" + std::to_string(s.manifest_bytes);
  *j += ",\"unique_chunks\":" + std::to_string(s.unique_chunks);
  *j += ",\"tombstoned_chunks\":" + std::to_string(s.tombstoned_chunks);
  *j += ",\"ampl_ratio\":" + std::to_string(s.ampl_ratio);
  *j += '}';
}

}  // namespace

extern "C" {

EBBACKUP_WORKBENCH_API int ebbackup_workbench_init_repo_json(const char* repo_path,
                                                               uint32_t flags,
                                                               char* out, size_t out_cap) {
  if (!repo_path) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "repo_path required"), out, out_cap);
  const EbStatus st = eb_backup_init_repo_ex(repo_path, flags);
  if (st != EB_OK) return CopyOut(ErrJson(st, "init_repo failed"), out, out_cap);
  std::string j = "{\"ok\":true,\"path\":";
  JsonEscape(repo_path, &j);
  j += '}';
  return CopyOut(j, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_repo_info_json(EbBackupEngine* eng, char* out,
                                                               size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  EbRepoStats stats{};
  const EbStatus st = eb_backup_repo_stats(eng, &stats);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  std::string j = "{\"ok\":true,\"open\":true";
  AppendRepoStatsJson(stats, &j);
  j += ",\"abi_version\":" + std::to_string(eb_backup_abi_version());
  j += '}';
  return CopyOut(j, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_list_snapshots_json(EbBackupEngine* eng, char* out,
                                                                    size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  EbSnapshotInfo* snaps = nullptr;
  size_t count = 0;
  const EbStatus st = eb_backup_list_snapshots(eng, &snaps, &count);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  std::unordered_map<uint64_t, ebbackup::catalog::SnapshotMetaRecord> meta_map;
  const char* repo_path = eb_backup_engine_repo_path(eng);
  if (repo_path && repo_path[0] != '\0') {
    (void)ebbackup::catalog::LoadSnapshotMetaMap(repo_path, &meta_map);
  }
  std::string j = "{\"ok\":true,\"snapshots\":[";
  for (size_t i = 0; i < count; ++i) {
    if (i) j += ',';
    j += '{';
    j += "\"txn_id\":" + std::to_string(snaps[i].txn_id);
    j += ",\"created_at_unix\":" + std::to_string(snaps[i].created_at_unix);
    j += ",\"manifest_crc32\":" + std::to_string(snaps[i].manifest_crc32);
    j += ",\"file_count\":" + std::to_string(snaps[i].file_count);
    const auto it = meta_map.find(snaps[i].txn_id);
    if (it != meta_map.end()) {
      if (!it->second.job_id.empty()) {
        j += ",\"job_id\":";
        JsonEscape(it->second.job_id, &j);
      }
      if (it->second.retention_tag != 0) {
        j += ",\"retention_tag\":" + std::to_string(it->second.retention_tag);
      }
      if (it->second.immutable_until_unix != 0) {
        j += ",\"immutable_until_unix\":" + std::to_string(it->second.immutable_until_unix);
      }
    }
    j += '}';
  }
  j += "],\"count\":" + std::to_string(count) + '}';
  eb_backup_free_snapshots(snaps);
  return CopyOut(j, out, out_cap);
}

namespace {

const char* ManifestFileTypeName(uint8_t t) {
  switch (t) {
    case 1:
      return "dir";
    case 2:
      return "symlink";
    case 3:
      return "fifo";
    case 4:
      return "block";
    case 5:
      return "char";
    default:
      return "file";
  }
}

}  // namespace

EBBACKUP_WORKBENCH_API int ebbackup_workbench_list_manifest_files_json(
    EbBackupEngine* eng, uint64_t txn_id, char* out, size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  EbManifestFileInfo* files = nullptr;
  size_t count = 0;
  uint64_t manifest_txn = 0;
  const EbStatus st =
      eb_backup_list_manifest_files(eng, txn_id, &manifest_txn, &files, &count);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  std::string j = "{\"ok\":true,\"txn_id\":" + std::to_string(manifest_txn);
  j += ",\"files\":[";
  uint64_t total_bytes = 0;
  for (size_t i = 0; i < count; ++i) {
    if (i) j += ',';
    j += '{';
    j += "\"relative_path\":";
    JsonEscape(files[i].relative_path ? files[i].relative_path : "", &j);
    j += ",\"size\":" + std::to_string(files[i].size);
    j += ",\"file_type\":\"" + std::string(ManifestFileTypeName(files[i].file_type)) + '"';
    j += ",\"mtime_unix\":" + std::to_string(files[i].mtime_unix);
    j += ",\"chunk_count\":" + std::to_string(files[i].chunk_count);
    j += '}';
    total_bytes += files[i].size;
  }
  j += "],\"count\":" + std::to_string(count);
  j += ",\"total_bytes\":" + std::to_string(total_bytes) + '}';
  eb_backup_free_manifest_files(files, count);
  return CopyOut(j, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_run_backup_json(EbBackupEngine* eng,
                                                                const char* source_path,
                                                                int incremental,
                                                                uint32_t flags,
                                                                char* out,
                                                                size_t out_cap) {
  if (!eng || !source_path) {
    return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine and source_path required"), out, out_cap);
  }
  const EbStatus st =
      incremental ? eb_backup_run_incremental_ex(eng, source_path, flags)
                  : eb_backup_run_ex(eng, source_path, flags);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  EbBackupStats stats{};
  if (eb_backup_get_stats(eng, &stats) != EB_OK) {
    return CopyOut("{\"ok\":true}", out, out_cap);
  }
  std::string j = "{\"ok\":true";
  AppendStatsJson(stats, &j);
  j += '}';
  return CopyOut(j, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_run_restore_json(EbBackupEngine* eng,
                                                                 const char* dest_path,
                                                                 uint64_t txn_id,
                                                                 uint32_t flags,
                                                                 char* out,
                                                                 size_t out_cap) {
  return ebbackup_workbench_run_restore_ex_json(eng, dest_path, txn_id, flags, nullptr,
                                                nullptr, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_run_restore_ex_json(
    EbBackupEngine* eng, const char* dest_path, uint64_t txn_id, uint32_t flags,
    const char* filter_json, const char* remap_json, char* out, size_t out_cap) {
  if (!eng || !dest_path) {
    return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine and dest_path required"), out,
                   out_cap);
  }
  if ((filter_json && filter_json[0]) || (remap_json && remap_json[0])) {
    const EbStatus cfg_st =
        eb_backup_set_filter_json_and_remap(eng, filter_json, remap_json);
    if (cfg_st != EB_OK) return CopyOut(LastErrJson(eng, cfg_st), out, out_cap);
  }
  const EbStatus st = txn_id ? eb_backup_restore_at(eng, dest_path, txn_id, flags)
                             : eb_backup_restore_ex(eng, dest_path, flags);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  return CopyOut("{\"ok\":true,\"restored\":true}", out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_preview_restore_json(
    EbBackupEngine* eng, uint64_t txn_id, const char* filter_json, char* out,
    size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  if (filter_json && filter_json[0]) {
    const EbStatus cfg_st = eb_backup_set_filter_json(eng, filter_json);
    if (cfg_st != EB_OK) return CopyOut(LastErrJson(eng, cfg_st), out, out_cap);
  }
  EbRestorePreviewReport report{};
  const EbStatus st = eb_backup_preview_restore_at(eng, txn_id, &report);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  std::string j = "{\"ok\":true";
  j += ",\"file_count\":" + std::to_string(report.file_count);
  j += ",\"dir_count\":" + std::to_string(report.dir_count);
  j += ",\"total_bytes\":" + std::to_string(report.total_bytes);
  j += ",\"txn_id\":" + std::to_string(txn_id);
  j += '}';
  return CopyOut(j, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_preview_in_place_json(
    EbBackupEngine* eng, uint64_t txn_id, const char* target_root,
    const char* filter_json, const char* in_place_options_json, char* out,
    size_t out_cap) {
  if (!eng || !target_root) {
    return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine and target_root required"), out,
                   out_cap);
  }
  if (filter_json && filter_json[0]) {
    const EbStatus cfg_st = eb_backup_set_filter_json(eng, filter_json);
    if (cfg_st != EB_OK) return CopyOut(LastErrJson(eng, cfg_st), out, out_cap);
  }
  char* json = eb_backup_preview_in_place_json(eng, txn_id, target_root,
                                               in_place_options_json);
  if (!json) return CopyOut(ErrJson(EB_ERROR_INTERNAL, "preview failed"), out, out_cap);
  const int rc = CopyOut(json, out, out_cap);
  eb_backup_free_string(json);
  return rc;
}

namespace {

std::string BuildInPlaceApplyOptionsJson(const char* conflict_policy,
                                         const char* orphan_policy,
                                         const char* extra_json) {
  std::ostringstream out;
  out << '{';
  bool first = true;
  auto emit = [&](const std::string& piece) {
    if (piece.empty()) return;
    if (!first) out << ',';
    first = false;
    out << piece;
  };
  if (conflict_policy && conflict_policy[0]) {
    emit(std::string("\"conflict_policy\":\"") + conflict_policy + '"');
  }
  if (orphan_policy && orphan_policy[0]) {
    emit(std::string("\"orphan_policy\":\"") + orphan_policy + '"');
  }
  if (extra_json && extra_json[0]) {
    std::string extra = extra_json;
    if (!extra.empty() && extra.front() == '{') extra.erase(0, 1);
    while (!extra.empty() && (extra.back() == '}' || std::isspace(static_cast<unsigned char>(extra.back())))) {
      extra.pop_back();
    }
    emit(extra);
  }
  out << '}';
  return out.str();
}

}  // namespace

EBBACKUP_WORKBENCH_API int ebbackup_workbench_apply_in_place_json(
    EbBackupEngine* eng, uint64_t txn_id, const char* target_root,
    const char* conflict_policy, const char* orphan_policy,
    const char* filter_json, const char* in_place_options_json, char* out,
    size_t out_cap) {
  if (!eng || !target_root) {
    return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine and target_root required"), out,
                   out_cap);
  }
  if (filter_json && filter_json[0]) {
    const EbStatus cfg_st = eb_backup_set_filter_json(eng, filter_json);
    if (cfg_st != EB_OK) return CopyOut(LastErrJson(eng, cfg_st), out, out_cap);
  }
  const std::string options =
      BuildInPlaceApplyOptionsJson(conflict_policy, orphan_policy, in_place_options_json);
  char* json = eb_backup_apply_in_place_json(eng, txn_id, target_root, options.c_str());
  if (!json) return CopyOut(ErrJson(EB_ERROR_INTERNAL, "apply failed"), out, out_cap);
  const int rc = CopyOut(json, out, out_cap);
  eb_backup_free_string(json);
  return rc;
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_export_delta_json(
    const char* repo_path, const char* bundle_path, uint64_t base_txn_id,
    uint64_t target_txn_id, const char* password, char* out, size_t out_cap) {
  if (!repo_path || !bundle_path || base_txn_id == 0) {
    return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "repo_path, bundle_path, base_txn required"),
                   out, out_cap);
  }
  EbBackupEngine* eng = eb_backup_open(repo_path);
  if (eng && password && password[0]) {
    eb_backup_set_password(eng, password);
  }
  uint32_t flags = 0;
  if (password && password[0]) flags |= EB_BACKUP_FLAG_ENCRYPT;
  char* json = eb_backup_export_delta_json(repo_path, bundle_path, base_txn_id, target_txn_id,
                                           flags);
  if (eng) eb_backup_close(eng);
  if (!json) return CopyOut(ErrJson(EB_ERROR_INTERNAL, "export delta failed"), out, out_cap);
  const int rc = CopyOut(json, out, out_cap);
  eb_backup_free_string(json);
  return rc;
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_import_delta_json(
    const char* base_path, const char* delta_path, const char* out_repo_path,
    char* out, size_t out_cap) {
  if (!base_path || !delta_path || !out_repo_path) {
    return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "base, delta, out_repo required"), out,
                   out_cap);
  }
  char* json = eb_backup_import_delta_json(base_path, delta_path, out_repo_path);
  if (!json) return CopyOut(ErrJson(EB_ERROR_INTERNAL, "import delta failed"), out, out_cap);
  const int rc = CopyOut(json, out, out_cap);
  eb_backup_free_string(json);
  return rc;
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_verify_json(EbBackupEngine* eng, uint64_t txn_id,
                                                            uint32_t flags, char* out,
                                                            size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  const EbStatus st =
      txn_id ? eb_backup_verify_at(eng, txn_id) : eb_backup_verify_ex(eng, flags);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  return CopyOut("{\"ok\":true,\"verified\":true}", out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_recover_json(EbBackupEngine* eng, char* out,
                                                             size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  const EbStatus st = eb_backup_recover(eng);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  return CopyOut("{\"ok\":true,\"recovered\":true}", out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_compact_json(EbBackupEngine* eng, int dry_run,
                                                             char* out, size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  EbCompactReport report{};
  const EbStatus st = eb_backup_compact(eng, dry_run, &report);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  std::string j = "{\"ok\":true,\"dry_run\":" + std::string(dry_run ? "true" : "false");
  j += ",\"physical_before\":" + std::to_string(report.physical_before);
  j += ",\"physical_after\":" + std::to_string(report.physical_after);
  j += ",\"live_bytes\":" + std::to_string(report.live_bytes);
  j += ",\"records_copied\":" + std::to_string(report.records_copied);
  j += ",\"ampl_ratio_before\":" + std::to_string(report.ampl_ratio_before);
  j += ",\"ampl_ratio_after\":" + std::to_string(report.ampl_ratio_after);
  j += '}';
  return CopyOut(j, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_gc_orphans_json(EbBackupEngine* eng, int dry_run,
                                                                char* out, size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  EbOrphanGcReport report{};
  const EbStatus st = eb_backup_gc_orphans_ex(eng, dry_run, &report);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  std::string j = std::string("{\"ok\":true,\"dry_run\":") + (dry_run ? "true" : "false");
  j += ",\"referenced_count\":" + std::to_string(report.referenced_count);
  j += ",\"orphan_count\":" + std::to_string(report.orphan_count);
  j += ",\"tombstoned_count\":" + std::to_string(report.tombstoned_count);
  j += '}';
  return CopyOut(j, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_prune_snapshots_json(
    EbBackupEngine* eng, const char* retention_tiers, int retain_min, int dry_run, char* out,
    size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  EbPruneReport report{};
  const EbStatus st =
      eb_backup_prune_snapshots(eng, retention_tiers, retain_min, dry_run, &report);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  std::string j = "{\"ok\":true,\"dry_run\":" + std::string(dry_run ? "true" : "false");
  j += ",\"kept_count\":" + std::to_string(report.kept_count);
  j += ",\"pruned_count\":" + std::to_string(report.pruned_count);
  j += '}';
  return CopyOut(j, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_run_maintenance_wizard_json(
    EbBackupEngine* eng, const char* options_json, char* out, size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  ebbackup::MaintenanceWizardOptions cpp_opts{};
  if (options_json && options_json[0]) {
    const ebbackup::Status parse_st =
        ebbackup::ParseMaintenanceWizardJson(options_json, &cpp_opts);
    if (!parse_st.ok()) {
      return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, parse_st.message().c_str()), out,
                     out_cap);
    }
  }
  EbMaintenanceWizardOptions opts{};
  opts.run_prune = cpp_opts.run_prune ? 1 : 0;
  opts.run_gc = cpp_opts.run_gc ? 1 : 0;
  opts.run_compact = cpp_opts.run_compact ? 1 : 0;
  opts.dry_run_only = cpp_opts.dry_run_only ? 1 : 0;
  opts.verify_after = cpp_opts.verify_after ? 1 : 0;
  opts.retain_min = cpp_opts.retain_min;
  opts.retention_tiers = cpp_opts.retention_tiers.c_str();
  EbMaintenanceWizardReport report{};
  const EbStatus st = eb_backup_run_maintenance_wizard(eng, &opts, &report);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  std::string j = "{\"ok\":true";
  AppendRepoStatsJson(report.stats_before, &j);
  j += ",\"stats_after\":{";
  j += "\"physical_bytes\":" + std::to_string(report.stats_after.physical_bytes);
  j += ",\"live_bytes\":" + std::to_string(report.stats_after.live_bytes);
  j += ",\"orphan_bytes\":" + std::to_string(report.stats_after.orphan_bytes);
  j += ",\"manifest_bytes\":" + std::to_string(report.stats_after.manifest_bytes);
  j += ",\"unique_chunks\":" + std::to_string(report.stats_after.unique_chunks);
  j += ",\"tombstoned_chunks\":" + std::to_string(report.stats_after.tombstoned_chunks);
  j += ",\"ampl_ratio\":" + std::to_string(report.stats_after.ampl_ratio);
  j += '}';
  j += ",\"prune\":{\"kept_count\":" + std::to_string(report.prune.kept_count);
  j += ",\"pruned_count\":" + std::to_string(report.prune.pruned_count) + '}';
  j += ",\"gc\":{\"referenced_count\":" + std::to_string(report.gc.referenced_count);
  j += ",\"orphan_count\":" + std::to_string(report.gc.orphan_count);
  j += ",\"tombstoned_count\":" + std::to_string(report.gc.tombstoned_count) + '}';
  j += ",\"compact\":{\"physical_before\":" + std::to_string(report.compact.physical_before);
  j += ",\"physical_after\":" + std::to_string(report.compact.physical_after);
  j += ",\"ampl_ratio_before\":" + std::to_string(report.compact.ampl_ratio_before);
  j += ",\"ampl_ratio_after\":" + std::to_string(report.compact.ampl_ratio_after) + '}';
  j += ",\"compact_skipped\":" + std::string(report.compact_skipped ? "true" : "false");
  j += ",\"verify_ran\":" + std::string(report.verify_ran ? "true" : "false");
  j += ",\"verify_ok\":" + std::string(report.verify_ok ? "true" : "false");
  j += '}';
  return CopyOut(j, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_runtime_info_json(char* out, size_t out_cap) {
  std::string j = "{\"ok\":true,\"abi_version\":" + std::to_string(eb_backup_abi_version());
  j += ",\"workbench\":\"ebbackup\"}";
  return CopyOut(j, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_last_error_json(EbBackupEngine* eng, char* out,
                                                                size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  char* err = eb_backup_last_error(eng);
  std::string j = "{\"ok\":true,\"error\":";
  if (err && err[0]) {
    JsonEscape(err, &j);
  } else {
    j += "null";
  }
  j += '}';
  if (err) eb_backup_free_string(err);
  return CopyOut(j, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_get_stats_json(EbBackupEngine* eng, char* out,
                                                               size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  EbBackupStats stats{};
  const EbStatus st = eb_backup_get_stats(eng, &stats);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  std::string j = "{\"ok\":true";
  AppendStatsJson(stats, &j);
  j += '}';
  return CopyOut(j, out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_build_path_index_json(
    EbBackupEngine* eng, int full_rebuild, char* out, size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  const EbStatus st = eb_backup_build_path_index(eng, full_rebuild);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  return CopyOut("{\"ok\":true}", out, out_cap);
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_query_path_history_json(
    EbBackupEngine* eng, const char* path, uint64_t offset, uint64_t limit,
    char* out, size_t out_cap) {
  if (!eng || !path) {
    return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine and path required"), out, out_cap);
  }
  char* json = eb_backup_query_path_history_json(eng, path, offset, limit);
  if (!json) return CopyOut(ErrJson(EB_ERROR_INTERNAL, "query failed"), out, out_cap);
  const int rc = CopyOut(json, out, out_cap);
  eb_backup_free_string(json);
  return rc;
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_list_manifest_page_json(
    EbBackupEngine* eng, uint64_t txn_id, const char* prefix, uint64_t offset,
    uint64_t limit, char* out, size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  char* json =
      eb_backup_list_manifest_files_page_json(eng, txn_id, prefix, offset, limit);
  if (!json) return CopyOut(ErrJson(EB_ERROR_INTERNAL, "page query failed"), out, out_cap);
  const int rc = CopyOut(json, out, out_cap);
  eb_backup_free_string(json);
  return rc;
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_diff_snapshots_json(
    EbBackupEngine* eng, uint64_t txn_a, uint64_t txn_b, char* out, size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  char* json = eb_backup_diff_snapshots_json(eng, txn_a, txn_b);
  if (!json) return CopyOut(ErrJson(EB_ERROR_INTERNAL, "diff failed"), out, out_cap);
  const int rc = CopyOut(json, out, out_cap);
  eb_backup_free_string(json);
  return rc;
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_export_restore_report_json(
    EbBackupEngine* eng, char* out, size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  char* json = eb_backup_export_restore_report_json(eng);
  if (!json) return CopyOut(ErrJson(EB_ERROR_INTERNAL, "report export failed"), out, out_cap);
  const int rc = CopyOut(json, out, out_cap);
  eb_backup_free_string(json);
  return rc;
}

EBBACKUP_WORKBENCH_API int ebbackup_workbench_get_backup_report_json(
    EbBackupEngine* eng, uint64_t txn_id, char* out, size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  char* json = eb_backup_get_backup_report_json(eng, txn_id);
  if (!json) return CopyOut(ErrJson(EB_ERROR_INTERNAL, "backup report failed"), out, out_cap);
  const int rc = CopyOut(json, out, out_cap);
  eb_backup_free_string(json);
  return rc;
}

namespace {

std::string ExtractJsonStringField(const std::string& json, const char* key) {
  const std::string needle = std::string("\"") + key + "\":";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return "";
  size_t i = pos + needle.size();
  while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
  if (i >= json.size() || json[i] != '"') return "";
  ++i;
  std::string out;
  while (i < json.size()) {
    if (json[i] == '"') break;
    if (json[i] == '\\' && i + 1 < json.size()) {
      out += json[i + 1];
      i += 2;
      continue;
    }
    out += json[i++];
  }
  return out;
}

}  // namespace

EBBACKUP_WORKBENCH_API int ebbackup_workbench_set_backup_hooks_json(
    EbBackupEngine* eng, const char* json, char* out, size_t out_cap) {
  if (!eng) return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine required"), out, out_cap);
  std::string pre;
  std::string post;
  if (json && json[0] != '\0') {
    pre = ExtractJsonStringField(json, "pre_backup_cmd");
    post = ExtractJsonStringField(json, "post_backup_cmd");
    (void)eb_backup_set_plugins_json(eng, json);
  }
  eb_backup_set_backup_hooks(eng, pre.empty() ? nullptr : pre.c_str(),
                             post.empty() ? nullptr : post.c_str());
  return CopyOut("{\"ok\":true}", out, out_cap);
}

}  // extern "C"
