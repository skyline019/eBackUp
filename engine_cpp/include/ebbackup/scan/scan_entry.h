#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"

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

  bool needs_chunking() const { return type == FileType::kRegular; }

  ManifestFileEntry ToManifestSkeleton() const;
};

Status ScanDirectory(const std::string& source_root,
                     std::vector<ScanEntry>* out);

}  // namespace ebbackup
