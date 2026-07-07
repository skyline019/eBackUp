#pragma once

#include <cstdint>
#include <string>

#include "ebbackup/common/status.h"
#include "ebbackup/state/superblock.h"

namespace ebbackup {

class ChunkStore;

struct CompactReport {
  uint64_t physical_before{0};
  uint64_t physical_after{0};
  uint64_t live_bytes{0};
  uint64_t records_copied{0};
  double ampl_ratio_before{0.0};
  double ampl_ratio_after{0.0};
};

Status CompactEbPackStore(const std::string& repo_path,
                          const BackupSuperBlock& sb, bool dry_run,
                          CompactReport* report);

Status CompactChunkStore(const std::string& repo_path, bool dry_run,
                         CompactReport* report);

Status WaitForRepoIdle(const std::string& repo_path, int timeout_seconds);

}  // namespace ebbackup
