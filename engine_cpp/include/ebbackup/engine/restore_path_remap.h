#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"

namespace ebbackup {

enum class RestoreLayoutMode : uint8_t {
  kKeep = 0,
  kStripPrefix = 1,
  kFlatten = 2,
  kRemapPrefix = 3,
};

enum class RestoreConflictPolicy : uint8_t {
  kFail = 0,
  kSkip = 1,
  kSuffix = 2,
};

struct RestorePathRemap {
  RestoreLayoutMode mode{RestoreLayoutMode::kKeep};
  std::string strip_prefix;
  std::string map_from;
  std::string map_to;
  RestoreConflictPolicy conflict{RestoreConflictPolicy::kFail};

  bool HasRemap() const {
    return mode != RestoreLayoutMode::kKeep;
  }
};

struct SymlinkRemap {
  std::string map_from;
  std::string map_to;

  bool HasRemap() const { return !map_from.empty(); }
};

struct ResolvedDestPath {
  bool skip{false};
  std::string path;
};

Status ResolveDestRelativePath(const std::string& manifest_rel,
                               FileType file_type,
                               const RestorePathRemap& remap,
                               ResolvedDestPath* out);

Status AssignDestPathWithConflict(
    const std::string& candidate, RestoreConflictPolicy conflict,
    std::unordered_map<std::string, uint32_t>* seen, std::string* assigned);

std::string ApplySymlinkTargetRemap(const std::string& target,
                                    const SymlinkRemap& remap);

std::vector<std::string> CollapseIncludePaths(
    const std::vector<std::string>& paths);

}  // namespace ebbackup
