#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/report/backup_report.h"
#include "ebbackup/scan/scan_hint_options.h"

namespace ebbackup {

struct ScanEntry {
  std::string absolute_path;
  std::string relative_path;
  FileType type{FileType::kRegular};
  uint64_t size{0};
  uint32_t mode{0};
  uint32_t uid{0xFFFFFFFFu};
  uint32_t gid{0xFFFFFFFFu};
  int64_t mtime_unix{0};
  int64_t atime_unix{0};
  std::string symlink_target;
  uint32_t device_major{0};
  uint32_t device_minor{0};

  std::string security_descriptor_b64;
  uint64_t inode_id{0};
  uint32_t reparse_tag{0};
  std::string reparse_target;
  std::string stream_name;

  bool needs_chunking() const { return type == FileType::kRegular; }

  ManifestFileEntry ToManifestSkeleton() const;
};

struct ScanResult {
  std::vector<ScanEntry> entries;
  std::vector<report::BackupPathIssue> issues;
};

Status ScanDirectory(const std::string& source_root, ScanResult* out,
                     const ScanHintOptions* hint_opts = nullptr);

#ifdef _WIN32
Status EnrichScanEntriesWinMeta(std::vector<ScanEntry>* entries,
                                std::vector<report::BackupPathIssue>* issues);
#endif

}  // namespace ebbackup
