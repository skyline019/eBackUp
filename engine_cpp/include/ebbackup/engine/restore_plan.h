#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ebbackup/common/status.h"
#include "ebbackup/engine/manifest.h"
#include "ebbackup/engine/restore_engine.h"

namespace ebbackup {

struct RestorePlanBuildResult {
  std::vector<std::pair<ManifestFileEntry, std::string>> entries;
  std::unordered_map<std::string, std::string> dest_rel_by_manifest;
};

Status BuildRestorePlan(const std::vector<ManifestFileEntry>& files_in,
                        const RestoreOptions& options,
                        RestorePlanBuildResult* out);

}  // namespace ebbackup
