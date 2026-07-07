#pragma once

#include <cstdint>
#include <string>

#include "ebbackup/common/status.h"

namespace ebbackup {

struct RepoStats {
  uint64_t physical_bytes{0};
  uint64_t live_bytes{0};
  uint64_t orphan_bytes{0};
  uint64_t manifest_bytes{0};
  uint64_t unique_chunks{0};
  uint64_t tombstoned_chunks{0};
  double ampl_ratio{0.0};
};

Status ComputeRepoStats(const std::string& repo_path, RepoStats* out);

}  // namespace ebbackup
