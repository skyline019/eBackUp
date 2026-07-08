#pragma once

#include <algorithm>
#include <filesystem>
#include <string>

#include "ebbackup/common/status.h"
#include "ebbackup/common/path_encoding.h"

namespace ebbackup {

inline std::string NormalizeRepoPath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}

inline std::string RepoJoinUtf8(const std::string& repo, const std::string& name) {
  return PathToUtf8(PathFromUtf8(repo) / PathFromUtf8(name));
}

inline Status RelativePathFromRoot(const std::string& source_root,
                                   const std::string& file_path,
                                   std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  try {
    *out = NormalizeRepoPath(PathToUtf8(std::filesystem::relative(
        PathFromUtf8(file_path), PathFromUtf8(source_root))));
    return Status::Ok();
  } catch (const std::filesystem::filesystem_error& e) {
    return Status::InvalidArgument(std::string("relative path failed: ") +
                                     e.what());
  }
}

// Literal prefix strip — does not follow junctions/symlinks (Windows scan/backup).
inline Status RelativePathFromRootNoFollow(const std::string& source_root,
                                           const std::string& file_path,
                                           std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  std::string root = NormalizeRepoPath(source_root);
  std::string file = NormalizeRepoPath(file_path);
  while (root.size() > 1 && root.back() == '/') root.pop_back();
  if (root.empty()) return Status::InvalidArgument("empty source root");
  if (root == file) {
    *out = ".";
    return Status::Ok();
  }
  const std::string prefix = root + "/";
  if (file.size() > prefix.size() && file.compare(0, prefix.size(), prefix) == 0) {
    *out = file.substr(prefix.size());
    return Status::Ok();
  }
  return RelativePathFromRoot(source_root, file_path, out);
}

}  // namespace ebbackup
