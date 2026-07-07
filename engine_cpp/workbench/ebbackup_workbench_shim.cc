#include "ebbackup/ebbackup_workbench.h"

#include <cstring>
#include <string>

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
  std::string j = "{\"ok\":true,\"snapshots\":[";
  for (size_t i = 0; i < count; ++i) {
    if (i) j += ',';
    j += '{';
    j += "\"txn_id\":" + std::to_string(snaps[i].txn_id);
    j += ",\"created_at_unix\":" + std::to_string(snaps[i].created_at_unix);
    j += ",\"manifest_crc32\":" + std::to_string(snaps[i].manifest_crc32);
    j += ",\"file_count\":" + std::to_string(snaps[i].file_count);
    j += '}';
  }
  j += "],\"count\":" + std::to_string(count) + '}';
  eb_backup_free_snapshots(snaps);
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
  if (!eng || !dest_path) {
    return CopyOut(ErrJson(EB_ERROR_INVALID_ARGUMENT, "engine and dest_path required"), out, out_cap);
  }
  const EbStatus st = txn_id ? eb_backup_restore_at(eng, dest_path, txn_id, flags)
                             : eb_backup_restore_ex(eng, dest_path, flags);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  return CopyOut("{\"ok\":true,\"restored\":true}", out, out_cap);
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
  const EbStatus st = eb_backup_gc_orphans(eng, dry_run);
  if (st != EB_OK) return CopyOut(LastErrJson(eng, st), out, out_cap);
  return CopyOut(std::string("{\"ok\":true,\"dry_run\":") + (dry_run ? "true" : "false") + "}",
                  out, out_cap);
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

}  // extern "C"
