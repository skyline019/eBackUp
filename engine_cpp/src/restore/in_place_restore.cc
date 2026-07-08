#include "ebbackup/restore/in_place_restore.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "ebbackup/audit/merkle.h"
#include "ebbackup/catalog/path_index.h"
#include "ebbackup/common/path_encoding.h"
#include "ebbackup/common/path_util.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/engine/restore_engine.h"
#include "ebbackup/engine/restore_plan.h"
#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/store/chunk_store.h"

namespace ebbackup {
namespace restore {

namespace {

int64_t FileTimeToUnix(const std::filesystem::file_time_type& ft) {
  const auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
      ft - std::filesystem::file_time_type::clock::now() +
      std::chrono::system_clock::now());
  return sctp.time_since_epoch().count();
}

struct LiveEntryInfo {
  bool exists{false};
  FileType type{FileType::kRegular};
  uint64_t size{0};
  int64_t mtime_unix{0};
  std::string symlink_target;
};

Status ProbeLiveEntry(const std::filesystem::path& path, LiveEntryInfo* out) {
  if (!out) return Status::InvalidArgument("out is null");
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    out->exists = false;
    return Status::Ok();
  }
  out->exists = true;
  if (std::filesystem::is_symlink(path, ec)) {
    out->type = FileType::kSymlink;
    out->symlink_target =
        PathToUtf8(std::filesystem::read_symlink(path, ec));
    if (ec) return Status::IoError("read_symlink failed: " + ec.message());
    return Status::Ok();
  }
  if (std::filesystem::is_directory(path, ec)) {
    out->type = FileType::kDirectory;
    return Status::Ok();
  }
#ifndef _WIN32
  if (std::filesystem::is_fifo(path, ec)) {
    out->type = FileType::kFifo;
    return Status::Ok();
  }
  if (std::filesystem::is_block_file(path, ec)) {
    out->type = FileType::kBlock;
    return Status::Ok();
  }
  if (std::filesystem::is_character_file(path, ec)) {
    out->type = FileType::kChar;
    return Status::Ok();
  }
#endif
  out->type = FileType::kRegular;
  out->size = std::filesystem::file_size(path, ec);
  if (ec) return Status::IoError("file_size failed: " + ec.message());
  const auto ftime = std::filesystem::last_write_time(path, ec);
  if (ec) return Status::IoError("last_write_time failed: " + ec.message());
  out->mtime_unix = FileTimeToUnix(ftime);
  return Status::Ok();
}

std::string RelPathFromRoot(const std::filesystem::path& root,
                            const std::filesystem::path& abs) {
  std::error_code ec;
  const auto rel = std::filesystem::relative(abs, root, ec);
  if (ec) return PathToUtf8(abs);
  return NormalizeRepoPath(PathToUtf8(rel));
}

bool PathUnderIncludeScope(const std::string& rel,
                           const std::vector<std::string>& includes) {
  if (includes.empty()) return true;
  const std::string norm = NormalizeRepoPath(rel);
  for (const auto& inc : includes) {
    const std::string prefix = NormalizeRepoPath(inc);
    if (norm == prefix) return true;
    if (norm.size() > prefix.size() && norm[prefix.size()] == '/' &&
        norm.compare(0, prefix.size(), prefix) == 0) {
      return true;
    }
  }
  return false;
}

Status CollectOrphanEntries(const std::filesystem::path& target_root,
                            const std::unordered_set<std::string>& manifest_paths,
                            const BackupFilterOptions& filter,
                            std::vector<InPlacePreviewEntry>* orphans) {
  if (!orphans) return Status::InvalidArgument("orphans is null");
  orphans->clear();
  std::error_code ec;
  if (!std::filesystem::exists(target_root, ec)) return Status::Ok();
  for (auto it = std::filesystem::recursive_directory_iterator(
           target_root, std::filesystem::directory_options::skip_permission_denied,
           ec);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (ec) return Status::IoError("scan target failed: " + ec.message());
    const std::filesystem::path abs = it->path();
    const std::string rel = RelPathFromRoot(target_root, abs);
    if (!PathUnderIncludeScope(rel, filter.include_paths)) continue;
    if (manifest_paths.count(rel) > 0) continue;
    if (it->is_directory(ec)) continue;
    if (!it->is_regular_file(ec)) continue;
    InPlacePreviewEntry entry{};
    entry.path = rel;
    entry.action = "orphan";
    entry.reason = "not_in_snapshot";
    orphans->push_back(std::move(entry));
  }
  return Status::Ok();
}

void JsonEscape(const std::string& s, std::string* out) {
  if (!out) return;
  for (char c : s) {
    switch (c) {
      case '\\':
        out->append("\\\\");
        break;
      case '"':
        out->append("\\\"");
        break;
      case '\n':
        out->append("\\n");
        break;
      case '\r':
        out->append("\\r");
        break;
      case '\t':
        out->append("\\t");
        break;
      default:
        out->push_back(c);
        break;
    }
  }
}

bool LiveMatchesManifestEntry(const LiveEntryInfo& live,
                              const ManifestFileEntry& file) {
  if (!live.exists) return false;
  if (live.type != file.file_type) return false;
  if (file.file_type == FileType::kDirectory) return true;
  if (file.file_type == FileType::kSymlink) {
    return live.symlink_target == file.symlink_target;
  }
  if (file.file_type == FileType::kRegular) {
    return live.size == file.size && live.mtime_unix == file.mtime_unix;
  }
  return true;
}

bool LiveContentMatchesManifest(const std::filesystem::path& live_path,
                                const ManifestFileEntry& file,
                                const ChunkStore* store) {
  if (file.file_type != FileType::kRegular || !store) return false;
  return audit::VerifyRestoredFileChunks(PathToUtf8(live_path), file,
                                         const_cast<ChunkStore*>(store))
      .ok();
}

uint64_t ResolveBaseTxn(const BackupEngine& engine, uint64_t target_txn,
                        uint64_t requested) {
  if (requested != 0) return requested;
  std::vector<SnapshotEntry> snaps;
  if (!engine.ListSnapshots(&snaps).ok() || snaps.empty()) return 0;
  uint64_t best = 0;
  for (const auto& snap : snaps) {
    if (snap.txn_id < target_txn && snap.txn_id > best) best = snap.txn_id;
  }
  return best;
}

Status ResolveTargetTxn(const BackupEngine& engine, uint64_t txn_id,
                        uint64_t* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (txn_id != 0) {
    *out = txn_id;
    return Status::Ok();
  }
  ManifestDocument head;
  const Status st = engine.LoadManifest(0, &head);
  if (!st.ok()) return st;
  if (head.txn_id == 0) {
    return Status::InvalidArgument("cannot resolve target txn");
  }
  *out = head.txn_id;
  return Status::Ok();
}

Status ClearLivePathForOverwrite(const std::filesystem::path& abs) {
  std::error_code ec;
  if (!std::filesystem::exists(abs, ec)) return Status::Ok();
  if (std::filesystem::is_directory(abs, ec)) {
    std::filesystem::remove_all(abs, ec);
    if (ec) {
      return Status::IoError("cannot remove directory for overwrite: " + ec.message());
    }
    return Status::Ok();
  }
  if (std::filesystem::is_regular_file(abs, ec) ||
      std::filesystem::is_symlink(abs, ec)) {
    std::filesystem::remove(abs, ec);
    if (ec) {
      return Status::IoError("cannot remove path for overwrite: " + ec.message());
    }
  }
  return Status::Ok();
}

std::unordered_map<std::string, ManifestFileEntry> BuildManifestPathMap(
    const ManifestDocument& doc) {
  std::unordered_map<std::string, ManifestFileEntry> out;
  out.reserve(doc.files.size());
  for (const auto& f : doc.files) {
    out.emplace(NormalizeRepoPath(f.relative_path), f);
  }
  return out;
}

void BumpSummaryForAction(const InPlacePreviewEntry& entry,
                          const ManifestFileEntry& file,
                          InPlacePreviewSummary* summary) {
  if (entry.action == "add") {
    ++summary->add_count;
    if (file.file_type == FileType::kRegular) summary->bytes_to_write += file.size;
  } else if (entry.action == "modify") {
    ++summary->modify_count;
    if (file.file_type == FileType::kRegular) summary->bytes_to_write += file.size;
  } else if (entry.action == "unchanged") {
    ++summary->unchanged_count;
  } else if (entry.action == "both_changed") {
    ++summary->both_changed_count;
    ++summary->conflict_count;
    if (file.file_type == FileType::kRegular) summary->bytes_to_write += file.size;
  } else if (entry.action == "conflict") {
    ++summary->conflict_count;
  } else if (entry.action == "orphan") {
    ++summary->orphan_count;
  }
}

void ClassifyEntry(const ManifestFileEntry& file, const LiveEntryInfo& live,
                   InPlacePreviewEntry* entry, InPlacePreviewSummary* summary) {
  entry->base_action.clear();
  if (!live.exists) {
    entry->live_state = "missing";
    entry->action = "add";
    BumpSummaryForAction(*entry, file, summary);
    return;
  }
  if (live.type != file.file_type) {
    entry->live_state = "diverged";
    entry->action = "conflict";
    entry->reason = "type_mismatch";
    BumpSummaryForAction(*entry, file, summary);
    return;
  }
  if (file.file_type == FileType::kDirectory) {
    entry->live_state = "matches_target";
    entry->action = "unchanged";
    BumpSummaryForAction(*entry, file, summary);
    return;
  }
  if (file.file_type == FileType::kSymlink) {
    if (live.symlink_target != file.symlink_target) {
      entry->live_state = "diverged";
      entry->action = "modify";
    } else {
      entry->live_state = "matches_target";
      entry->action = "unchanged";
    }
    BumpSummaryForAction(*entry, file, summary);
    return;
  }
  if (file.file_type == FileType::kRegular) {
    if (live.size == file.size && live.mtime_unix == file.mtime_unix) {
      entry->live_state = "matches_target";
      entry->action = "unchanged";
    } else {
      entry->live_state = "diverged";
      entry->action = "modify";
    }
    BumpSummaryForAction(*entry, file, summary);
    return;
  }
  entry->live_state = "matches_target";
  entry->action = "unchanged";
  BumpSummaryForAction(*entry, file, summary);
}

void ClassifyThreeWayEntry(const ManifestFileEntry& target,
                           const ManifestFileEntry* base, DigestAlgo algo,
                           const ChunkStore* store,
                           const std::filesystem::path& live_path,
                           const LiveEntryInfo& live, InPlacePreviewEntry* entry,
                           InPlacePreviewSummary* summary) {
  if (!live.exists) {
    entry->live_state = "missing";
    if (!base) {
      entry->base_action = "absent";
      entry->action = "add";
    } else {
      const std::string base_hash = catalog::ComputeFileContentHashHex(*base, algo);
      const std::string target_hash =
          catalog::ComputeFileContentHashHex(target, algo);
      entry->base_action =
          (base_hash == target_hash) ? "same" : "changed";
      entry->action = "add";
    }
    BumpSummaryForAction(*entry, target, summary);
    return;
  }

  if (live.type != target.file_type) {
    entry->live_state = "diverged";
    entry->base_action = base ? "changed" : "absent";
    entry->action = "conflict";
    entry->reason = "type_mismatch";
    BumpSummaryForAction(*entry, target, summary);
    return;
  }

  if (!base) {
    ClassifyEntry(target, live, entry, summary);
    return;
  }

  const std::string base_hash = catalog::ComputeFileContentHashHex(*base, algo);
  const std::string target_hash = catalog::ComputeFileContentHashHex(target, algo);
  const bool target_changed = (base_hash != target_hash);

  if (!target_changed) {
    entry->base_action = "same";
    if (store && target.file_type == FileType::kRegular) {
      if (LiveContentMatchesManifest(live_path, target, store)) {
        entry->live_state = "matches_target";
        entry->action = "unchanged";
      } else {
        entry->live_state = "diverged";
        entry->action = "modify";
      }
    } else if (LiveMatchesManifestEntry(live, target)) {
      entry->live_state = "matches_target";
      entry->action = "unchanged";
    } else {
      entry->live_state = "diverged";
      entry->action = "modify";
    }
    BumpSummaryForAction(*entry, target, summary);
    return;
  }

  entry->base_action = "changed";
  if (store && target.file_type == FileType::kRegular) {
    if (LiveContentMatchesManifest(live_path, *base, store)) {
      entry->live_state = "matches_base";
      entry->action = "modify";
    } else if (LiveContentMatchesManifest(live_path, target, store)) {
      entry->live_state = "matches_target";
      entry->action = "unchanged";
    } else {
      entry->live_state = "diverged";
      entry->action = "both_changed";
      entry->reason = "both_changed";
    }
    BumpSummaryForAction(*entry, target, summary);
    return;
  }
  if (LiveMatchesManifestEntry(live, *base)) {
    entry->live_state = "matches_base";
    entry->action = "modify";
    BumpSummaryForAction(*entry, target, summary);
    return;
  }
  if (LiveContentMatchesManifest(live_path, target, store) ||
      LiveMatchesManifestEntry(live, target)) {
    entry->live_state = "matches_target";
    entry->action = "unchanged";
    BumpSummaryForAction(*entry, target, summary);
    return;
  }

  entry->live_state = "diverged";
  entry->action = "both_changed";
  entry->reason = "both_changed";
  BumpSummaryForAction(*entry, target, summary);
}

bool IsConflictAction(const std::string& action) {
  return action == "conflict" || action == "both_changed";
}

std::filesystem::path ResolvePreviewPath(const std::filesystem::path& target_root,
                                         const std::string& dest_rel) {
  std::string base_rel = dest_rel;
#ifdef _WIN32
  const size_t colon = base_rel.find(':');
  if (colon != std::string::npos && colon > 0) {
    base_rel = base_rel.substr(0, colon);
  }
#endif
  return target_root / PathFromUtf8(base_rel);
}

Status ValidateConflicts(const std::vector<InPlacePlannedEntry>& plan,
                         InPlaceConflictPolicy policy) {
  if (policy != InPlaceConflictPolicy::kFail) return Status::Ok();
  for (const auto& planned : plan) {
    if (IsConflictAction(planned.preview.action)) {
      return Status::Conflict("in-place conflict at " + planned.preview.path +
                              ": " + planned.preview.reason);
    }
  }
  return Status::Ok();
}

}  // namespace

Status BuildInPlacePlan(const BackupEngine& engine, uint64_t txn_id,
                        const std::string& target_root,
                        const RestoreOptions& options,
                        const InPlacePreviewOptions& preview_opts,
                        std::vector<InPlacePlannedEntry>* out,
                        uint64_t* resolved_base_txn,
                        bool* three_way_used,
                        uint64_t* resolved_target_txn) {
  if (!out) return Status::InvalidArgument("out is null");
  if (target_root.empty()) {
    return Status::InvalidArgument("target_root is required");
  }
  std::error_code ec;
  if (!std::filesystem::is_directory(PathFromUtf8(target_root), ec)) {
    return Status::InvalidArgument("target_root must be an existing directory");
  }

  uint64_t resolved_txn = 0;
  const Status txn_st = ResolveTargetTxn(engine, txn_id, &resolved_txn);
  if (!txn_st.ok()) return txn_st;
  if (resolved_target_txn) *resolved_target_txn = resolved_txn;

  ManifestDocument target_doc;
  const Status rd = engine.LoadManifest(resolved_txn, &target_doc);
  if (!rd.ok()) return rd;

  const uint64_t base_txn =
      ResolveBaseTxn(engine, resolved_txn, preview_opts.base_txn_id);
  const bool use_three_way =
      preview_opts.use_three_way && base_txn != 0 && base_txn != resolved_txn;
  if (resolved_base_txn) *resolved_base_txn = base_txn;
  if (three_way_used) *three_way_used = use_three_way;

  std::unordered_map<std::string, ManifestFileEntry> base_map;
  DigestAlgo digest_algo = DigestAlgo::kStandard;
  if (engine.chunk_store()) {
    digest_algo = engine.chunk_store()->digest_algo();
  }
  if (use_three_way) {
    ManifestDocument base_doc;
    const Status base_rd = engine.LoadManifest(base_txn, &base_doc);
    if (!base_rd.ok()) return base_rd;
    base_map = BuildManifestPathMap(base_doc);
  }

  RestorePlanBuildResult plan{};
  const Status plan_st = BuildRestorePlan(target_doc.files, options, &plan);
  if (!plan_st.ok()) return plan_st;

  out->clear();
  out->reserve(plan.entries.size());

  InPlacePreviewSummary scratch{};
  const std::filesystem::path target = PathFromUtf8(target_root);
  for (const auto& [file, dest_rel] : plan.entries) {
    InPlacePlannedEntry planned{};
    planned.file = file;
    planned.dest_rel = dest_rel;
    planned.preview.path = dest_rel;

    LiveEntryInfo live{};
    const std::filesystem::path abs = ResolvePreviewPath(target, dest_rel);
    const Status probe_st = ProbeLiveEntry(abs, &live);
    if (!probe_st.ok()) {
      planned.preview.action = "conflict";
      planned.preview.reason = probe_st.message();
      planned.preview.live_state = "diverged";
      BumpSummaryForAction(planned.preview, file, &scratch);
    } else if (use_three_way) {
      const std::string key = NormalizeRepoPath(file.relative_path);
      const ManifestFileEntry* base_entry = nullptr;
      auto it = base_map.find(key);
      if (it != base_map.end()) {
        base_entry = &it->second;
      } else {
        planned.preview.base_action = "absent";
      }
      ClassifyThreeWayEntry(file, base_entry, digest_algo, engine.chunk_store(),
                            abs, live, &planned.preview, &scratch);
    } else {
      ClassifyEntry(file, live, &planned.preview, &scratch);
    }
    out->push_back(std::move(planned));
  }

  std::unordered_set<std::string> manifest_paths;
  manifest_paths.reserve(out->size());
  for (const auto& planned : *out) {
    manifest_paths.insert(NormalizeRepoPath(planned.dest_rel));
  }
  std::vector<InPlacePreviewEntry> orphans;
  const Status orphan_st =
      CollectOrphanEntries(target, manifest_paths, options.filter, &orphans);
  if (!orphan_st.ok()) return orphan_st;
  for (auto& orphan : orphans) {
    InPlacePlannedEntry planned{};
    planned.dest_rel = orphan.path;
    planned.preview = std::move(orphan);
    out->push_back(std::move(planned));
  }
  return Status::Ok();
}

Status PreviewInPlaceRestore(const BackupEngine& engine, uint64_t txn_id,
                             const std::string& target_root,
                             const RestoreOptions& options,
                             const InPlacePreviewOptions& preview_opts,
                             InPlacePreviewReport* out) {
  if (!out) return Status::InvalidArgument("out is null");

  uint64_t base_txn = 0;
  bool three_way = false;
  uint64_t resolved_txn = 0;
  std::vector<InPlacePlannedEntry> plan;
  const Status plan_st = BuildInPlacePlan(engine, txn_id, target_root, options,
                                          preview_opts, &plan, &base_txn,
                                          &three_way, &resolved_txn);
  if (!plan_st.ok()) return plan_st;

  out->txn_id = resolved_txn;
  out->base_txn_id = base_txn;
  out->three_way = three_way;
  out->target_root = target_root;
  out->summary = {};
  out->entries.clear();
  out->entries.reserve(plan.size());
  for (const auto& planned : plan) {
    out->entries.push_back(planned.preview);
    BumpSummaryForAction(planned.preview, planned.file, &out->summary);
  }
  return Status::Ok();
}

Status ApplyInPlaceRestore(BackupEngine& engine, uint64_t txn_id,
                           const std::string& target_root,
                           const RestoreOptions& restore_opts,
                           const InPlacePreviewOptions& preview_opts,
                           const InPlaceApplyOptions& apply_opts,
                           InPlaceApplyReport* out) {
  if (!out) return Status::InvalidArgument("out is null");

  uint64_t base_txn = 0;
  bool three_way = false;
  uint64_t resolved_txn = 0;
  std::vector<InPlacePlannedEntry> plan;
  const Status plan_st = BuildInPlacePlan(engine, txn_id, target_root, restore_opts,
                                          preview_opts, &plan, &base_txn,
                                          &three_way, &resolved_txn);
  if (!plan_st.ok()) return plan_st;

  out->txn_id = resolved_txn;
  out->base_txn_id = base_txn;
  out->three_way = three_way;
  out->dry_run = apply_opts.dry_run;
  out->target_root = target_root;
  out->summary = {};
  out->entries.clear();
  out->issues.clear();
  out->entries.reserve(plan.size());

  for (const auto& planned : plan) {
    out->entries.push_back(planned.preview);
    const std::string& action = planned.preview.action;
    if (action == "add") ++out->summary.add_count;
    else if (action == "modify") ++out->summary.modify_count;
    else if (action == "unchanged") ++out->summary.unchanged_count;
    else if (action == "both_changed") {
      ++out->summary.both_changed_count;
      ++out->summary.conflict_count;
    } else if (action == "conflict") ++out->summary.conflict_count;
    else if (action == "orphan") ++out->summary.orphan_count;
  }

  if (apply_opts.dry_run) {
    for (const auto& planned : plan) {
      if (planned.preview.action == "orphan") {
        ++out->summary.skipped_count;
      }
    }
    return Status::Ok();
  }

  const Status conflict_st = ValidateConflicts(plan, apply_opts.conflict);
  if (!conflict_st.ok()) return conflict_st;

  if (!engine.chunk_store()) {
    return Status::InvalidArgument("chunk store unavailable");
  }

  RestoreEngine restore_engine(engine.repo_path(), engine.chunk_store());
  RestoreOptions opts = restore_opts;
  opts.snapshot_txn_id = resolved_txn;

  const std::filesystem::path target = PathFromUtf8(target_root);
  std::unordered_map<RestoreInodeKey, std::string, RestoreInodeKeyHash>
      inode_canonical;

  for (const auto& planned : plan) {
    const std::string& action = planned.preview.action;
    if (action == "orphan") {
      if (apply_opts.orphan == InPlaceOrphanPolicy::kDelete) {
        const std::filesystem::path abs =
            ResolvePreviewPath(target, planned.preview.path);
        std::error_code ec;
        if (std::filesystem::is_regular_file(abs, ec)) {
          if (!std::filesystem::remove(abs, ec)) {
            return Status::IoError("orphan delete failed: " + planned.preview.path);
          }
          ++out->summary.orphan_deleted_count;
          ++out->summary.applied_count;
        } else {
          ++out->summary.skipped_count;
        }
      } else {
        ++out->summary.skipped_count;
      }
      continue;
    }
    if (action == "unchanged") {
      continue;
    }
    if (IsConflictAction(action)) {
      if (apply_opts.conflict == InPlaceConflictPolicy::kOverwrite) {
        const std::filesystem::path abs =
            ResolvePreviewPath(target, planned.dest_rel);
        const Status clear_st = ClearLivePathForOverwrite(abs);
        if (!clear_st.ok()) {
          ++out->summary.failed_count;
          return clear_st;
        }
        const Status restore_st = restore_engine.RestorePlannedEntry(
            target, planned.file, planned.dest_rel, opts, &inode_canonical,
            &out->issues);
        if (!restore_st.ok()) {
          ++out->summary.failed_count;
          return restore_st;
        }
        ++out->summary.overwritten_count;
        ++out->summary.applied_count;
        if (planned.file.file_type == FileType::kRegular) {
          out->summary.bytes_written += planned.file.size;
        }
      } else {
        ++out->summary.skipped_count;
      }
      continue;
    }
    if (action != "add" && action != "modify") {
      ++out->summary.skipped_count;
      continue;
    }

    const Status restore_st = restore_engine.RestorePlannedEntry(
        target, planned.file, planned.dest_rel, opts, &inode_canonical,
        &out->issues);
    if (!restore_st.ok()) {
      ++out->summary.failed_count;
      return restore_st;
    }

    ++out->summary.applied_count;
    if (planned.file.file_type == FileType::kRegular) {
      out->summary.bytes_written += planned.file.size;
    }
  }

  if (restore_opts.verify_restored_content) {
    ManifestDocument doc;
    const Status rd = engine.LoadManifest(resolved_txn, &doc);
    if (!rd.ok()) return rd;
    std::vector<ManifestFileEntry> files = doc.files;
    if (restore_opts.filter.HasAnyFilter()) {
      std::vector<ManifestFileEntry> filtered;
      const Status filter_st =
          ApplyManifestFilter(restore_opts.filter, files, &filtered);
      if (!filter_st.ok()) return filter_st;
      files = std::move(filtered);
    }
    uint8_t subset_root[32]{};
    const Status merkle_st = audit::ComputeMerkleRootForFiles(
        files, subset_root, engine.chunk_store()->digest_algo());
    if (!merkle_st.ok()) return merkle_st;
    uint8_t actual_root[32]{};
    RestorePlanBuildResult built{};
    const Status built_st = BuildRestorePlan(files, restore_opts, &built);
    if (!built_st.ok()) return built_st;
    const std::unordered_map<std::string, std::string>* override_ptr =
        restore_opts.path_remap.HasRemap() ? &built.dest_rel_by_manifest
                                           : nullptr;
    const Status verify_st = audit::ComputeMerkleRootFromRestoredFiles(
        target_root, files, engine.chunk_store(), actual_root, override_ptr);
    if (!verify_st.ok()) return verify_st;
    if (std::memcmp(subset_root, actual_root, 32) != 0) {
      return Status::Corrupt("in-place restored content merkle mismatch");
    }
  }

  return Status::Ok();
}

std::string InPlacePreviewReportToJson(const InPlacePreviewReport& report) {
  std::ostringstream out;
  out << "{\"ok\":true";
  out << ",\"txn_id\":" << report.txn_id;
  out << ",\"base_txn_id\":" << report.base_txn_id;
  out << ",\"three_way\":" << (report.three_way ? "true" : "false");
  std::string escaped_root;
  JsonEscape(report.target_root, &escaped_root);
  out << ",\"target_root\":\"" << escaped_root << "\"";
  out << ",\"summary\":{";
  out << "\"add_count\":" << report.summary.add_count;
  out << ",\"modify_count\":" << report.summary.modify_count;
  out << ",\"unchanged_count\":" << report.summary.unchanged_count;
  out << ",\"conflict_count\":" << report.summary.conflict_count;
  out << ",\"both_changed_count\":" << report.summary.both_changed_count;
  out << ",\"skip_count\":" << report.summary.skip_count;
  out << ",\"orphan_count\":" << report.summary.orphan_count;
  out << ",\"bytes_to_write\":" << report.summary.bytes_to_write;
  out << "},\"entries\":[";
  for (size_t i = 0; i < report.entries.size(); ++i) {
    if (i > 0) out << ',';
    const auto& e = report.entries[i];
    std::string path_esc;
    std::string reason_esc;
    std::string base_esc;
    std::string live_esc;
    JsonEscape(e.path, &path_esc);
    JsonEscape(e.reason, &reason_esc);
    JsonEscape(e.base_action, &base_esc);
    JsonEscape(e.live_state, &live_esc);
    out << "{\"path\":\"" << path_esc << "\",\"action\":\"" << e.action << "\"";
    if (!e.reason.empty()) out << ",\"reason\":\"" << reason_esc << "\"";
    if (!e.base_action.empty()) out << ",\"base_action\":\"" << base_esc << "\"";
    if (!e.live_state.empty()) out << ",\"live_state\":\"" << live_esc << "\"";
    out << '}';
  }
  out << "]}";
  return out.str();
}

std::string InPlaceApplyReportToJson(const InPlaceApplyReport& report) {
  std::ostringstream out;
  out << "{\"ok\":true";
  out << ",\"txn_id\":" << report.txn_id;
  out << ",\"base_txn_id\":" << report.base_txn_id;
  out << ",\"three_way\":" << (report.three_way ? "true" : "false");
  out << ",\"dry_run\":" << (report.dry_run ? "true" : "false");
  std::string escaped_root;
  JsonEscape(report.target_root, &escaped_root);
  out << ",\"target_root\":\"" << escaped_root << "\"";
  out << ",\"summary\":{";
  out << "\"applied_count\":" << report.summary.applied_count;
  out << ",\"skipped_count\":" << report.summary.skipped_count;
  out << ",\"failed_count\":" << report.summary.failed_count;
  out << ",\"overwritten_count\":" << report.summary.overwritten_count;
  out << ",\"add_count\":" << report.summary.add_count;
  out << ",\"modify_count\":" << report.summary.modify_count;
  out << ",\"unchanged_count\":" << report.summary.unchanged_count;
  out << ",\"conflict_count\":" << report.summary.conflict_count;
  out << ",\"both_changed_count\":" << report.summary.both_changed_count;
  out << ",\"orphan_count\":" << report.summary.orphan_count;
  out << ",\"orphan_deleted_count\":" << report.summary.orphan_deleted_count;
  out << ",\"bytes_written\":" << report.summary.bytes_written;
  out << "},\"entries\":[";
  for (size_t i = 0; i < report.entries.size(); ++i) {
    if (i > 0) out << ',';
    const auto& e = report.entries[i];
    std::string path_esc;
    std::string reason_esc;
    std::string base_esc;
    std::string live_esc;
    JsonEscape(e.path, &path_esc);
    JsonEscape(e.reason, &reason_esc);
    JsonEscape(e.base_action, &base_esc);
    JsonEscape(e.live_state, &live_esc);
    out << "{\"path\":\"" << path_esc << "\",\"action\":\"" << e.action << "\"";
    if (!e.reason.empty()) out << ",\"reason\":\"" << reason_esc << "\"";
    if (!e.base_action.empty()) out << ",\"base_action\":\"" << base_esc << "\"";
    if (!e.live_state.empty()) out << ",\"live_state\":\"" << live_esc << "\"";
    out << '}';
  }
  out << "]}";
  return out.str();
}

}  // namespace restore
}  // namespace ebbackup
