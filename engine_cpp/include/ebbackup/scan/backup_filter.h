#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/scan/scan_entry.h"

namespace ebbackup {

struct BackupFilterOptions {
  std::vector<std::string> include_paths;
  std::vector<std::string> exclude_paths;
  std::vector<std::string> include_globs;
  std::vector<std::string> exclude_globs;
  // Deprecated: prefer exclude_globs. Filter files use exclude_glob; name_glob
  // is merged into exclude_globs on load. Programmatic name_globs still works.
  std::vector<std::string> name_globs;
  std::vector<std::string> extensions;
  uint64_t min_size{0};
  uint64_t max_size{UINT64_MAX};
  int64_t mtime_after{0};
  int64_t mtime_before{INT64_MAX};
  uint32_t uid_filter{0};

  bool HasAnyFilter() const;
};

Status LoadBackupFilterFromFile(const std::string& path, BackupFilterOptions* out);

Status ApplyBackupFilter(const BackupFilterOptions& filter,
                         std::vector<ScanEntry>* entries);

ScanEntry ManifestEntryToScanEntry(const ManifestFileEntry& entry);

Status ApplyManifestFilter(const BackupFilterOptions& filter,
                             const std::vector<ManifestFileEntry>& all_files,
                             std::vector<ManifestFileEntry>* out);

}  // namespace ebbackup
