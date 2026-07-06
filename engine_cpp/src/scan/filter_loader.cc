#include "ebbackup/scan/filter_loader.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace ebbackup {

Status LoadFilterFromFile(const std::string& path, BackupFilterOptions* out) {
  return LoadBackupFilterFromFile(path, out);
}

namespace {

void AppendFilterValues(int argc, char** argv, const char* flag,
                        std::vector<std::string>* out) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0 && i + 1 < argc) {
      out->push_back(argv[i + 1]);
    }
  }
}

const char* GetFlagValue(int argc, char** argv, const char* flag) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
  }
  return nullptr;
}

}  // namespace

void MergeCliFilterFlags(int argc, char** argv, BackupFilterOptions* filter) {
  if (!filter) return;
  AppendFilterValues(argc, argv, "--include", &filter->include_paths);
  AppendFilterValues(argc, argv, "--exclude", &filter->exclude_paths);
  AppendFilterValues(argc, argv, "--include-glob", &filter->include_globs);
  AppendFilterValues(argc, argv, "--exclude-glob", &filter->exclude_globs);
  AppendFilterValues(argc, argv, "--ext", &filter->extensions);
  if (const char* min_size = GetFlagValue(argc, argv, "--min-size")) {
    filter->min_size = std::strtoull(min_size, nullptr, 10);
  }
  if (const char* max_size = GetFlagValue(argc, argv, "--max-size")) {
    filter->max_size = std::strtoull(max_size, nullptr, 10);
  }
  if (const char* mtime_after = GetFlagValue(argc, argv, "--mtime-after")) {
    filter->mtime_after = std::strtoll(mtime_after, nullptr, 10);
  }
  if (const char* mtime_before = GetFlagValue(argc, argv, "--mtime-before")) {
    filter->mtime_before = std::strtoll(mtime_before, nullptr, 10);
  }
  if (const char* uid = GetFlagValue(argc, argv, "--uid")) {
    filter->uid_filter = static_cast<uint32_t>(std::strtoul(uid, nullptr, 10));
  }
}

Status ApplyFilterFileIfFlag(int argc, char** argv, const char* flag,
                             BackupFilterOptions* filter) {
  if (!filter) return Status::InvalidArgument("filter is null");
  const char* path = GetFlagValue(argc, argv, flag);
  if (!path) return Status::Ok();
  return LoadFilterFromFile(path, filter);
}

Status LoadFilterFromCli(int argc, char** argv, BackupFilterOptions* filter) {
  if (!filter) return Status::InvalidArgument("filter is null");
  *filter = BackupFilterOptions{};
  const Status file_st = ApplyFilterFileIfFlag(argc, argv, "--filter-file", filter);
  if (!file_st.ok()) return file_st;
  MergeCliFilterFlags(argc, argv, filter);
  return Status::Ok();
}

}  // namespace ebbackup
