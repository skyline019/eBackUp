#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {

class BackupEngine;

namespace catalog {

struct SnapshotReachabilityReport {
  uint64_t txn_id{0};
  bool reachable{false};
  uint64_t files_checked{0};
  uint64_t chunks_checked{0};
  uint64_t missing_chunk_count{0};
  std::vector<std::string> missing_chunk_hex;
};

Status AnalyzeSnapshotReachability(const BackupEngine& engine, uint64_t txn_id,
                                   SnapshotReachabilityReport* out);

std::string SnapshotReachabilityReportToJson(
    const SnapshotReachabilityReport& report);

}  // namespace catalog
}  // namespace ebbackup
