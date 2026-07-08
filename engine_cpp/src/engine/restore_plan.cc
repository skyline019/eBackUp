#include "ebbackup/engine/restore_plan.h"

#include <algorithm>

#include "ebbackup/common/path_util.h"
#include "ebbackup/engine/restore_path_remap.h"
#include "ebbackup/scan/backup_filter.h"
#include "ebbackup/winmeta/win_meta.h"

namespace ebbackup {

namespace {

int FileTypeOrder(const ManifestFileEntry& file) {
  if (file.file_type == FileType::kDirectory) {
#ifdef _WIN32
    if (file.reparse_tag != 0) return 2;
#endif
    return 0;
  }
  return 1;
}

}  // namespace

Status BuildRestorePlan(const std::vector<ManifestFileEntry>& files_in,
                        const RestoreOptions& options,
                        RestorePlanBuildResult* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->entries.clear();
  out->dest_rel_by_manifest.clear();

  std::vector<ManifestFileEntry> files = files_in;
  if (options.filter.HasAnyFilter()) {
    std::vector<ManifestFileEntry> filtered;
    const Status filter_st = ApplyManifestFilter(options.filter, files, &filtered);
    if (!filter_st.ok()) return filter_st;
    files = std::move(filtered);
  }

  std::stable_sort(files.begin(), files.end(),
                   [](const ManifestFileEntry& a, const ManifestFileEntry& b) {
                     const int oa = FileTypeOrder(a);
                     const int ob = FileTypeOrder(b);
                     if (oa != ob) return oa < ob;
                     return a.relative_path < b.relative_path;
                   });

  std::unordered_map<std::string, uint32_t> dest_path_seen;
  out->entries.reserve(files.size());

  for (const auto& file : files) {
#ifdef _WIN32
    if (file.file_type == FileType::kDirectory && file.reparse_tag != 0 &&
        options.reparse_policy.mode == winmeta::ReparseRestorePolicy::Mode::kSkip) {
      continue;
    }
#endif
    ResolvedDestPath resolved{};
    const Status map_st = ResolveDestRelativePath(
        file.relative_path, file.file_type, options.path_remap, &resolved);
    if (!map_st.ok()) return map_st;
    if (resolved.skip) continue;

    std::string dest_rel;
    const Status conflict_st = AssignDestPathWithConflict(
        resolved.path, options.path_remap.conflict, &dest_path_seen, &dest_rel);
    if (!conflict_st.ok()) return conflict_st;
    if (dest_rel.empty() &&
        options.path_remap.conflict == RestoreConflictPolicy::kSkip) {
      continue;
    }
    const std::string manifest_key = NormalizeRepoPath(file.relative_path);
    out->dest_rel_by_manifest[manifest_key] = dest_rel;
    out->entries.emplace_back(file, dest_rel);
  }
  return Status::Ok();
}

}  // namespace ebbackup
