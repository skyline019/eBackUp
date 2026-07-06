#pragma once

#include <algorithm>
#include <filesystem>
#include <string>

#include "ebbackup/common/status.h"

namespace ebbackup {

inline std::string NormalizeRepoPath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}

inline Status RelativePathFromRoot(const std::string& source_root,
                                   const std::string& file_path,
                                   std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  try {
    *out = NormalizeRepoPath(
        std::filesystem::relative(file_path, source_root).string());
    return Status::Ok();
  } catch (const std::filesystem::filesystem_error& e) {
    return Status::InvalidArgument(std::string("relative path failed: ") +
                                     e.what());
  }
}

}  // namespace ebbackup
