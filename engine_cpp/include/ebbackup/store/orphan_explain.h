#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {

class BackupEngine;

namespace store {

enum class OrphanReason {
  kUnreferenced,
  kTombstoned,
  kInterruptedHint,
};

struct OrphanExplainEntry {
  std::string chunk_hex;
  OrphanReason reason{OrphanReason::kUnreferenced};
  uint64_t bytes{0};
  uint64_t last_referenced_txn{0};
};

struct OrphanExplainReport {
  uint64_t total_orphans{0};
  uint64_t total_orphan_bytes{0};
  uint64_t unreferenced_count{0};
  uint64_t tombstoned_count{0};
  uint64_t interrupted_hint_count{0};
  std::vector<OrphanExplainEntry> samples;
};

Status BuildOrphanExplainReport(const BackupEngine& engine, uint64_t sample_limit,
                                OrphanExplainReport* out);

std::string OrphanExplainReportToJson(const OrphanExplainReport& report);

}  // namespace store
}  // namespace ebbackup
