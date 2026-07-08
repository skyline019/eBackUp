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
  uint64_t live_uncompressed_bytes{0};
  uint64_t live_stored_payload_bytes{0};
  double compress_ratio{1.0};
  uint64_t compressed_chunk_count{0};
  uint64_t raw_chunk_count{0};
  bool has_zstd_dict{false};
  uint64_t zstd_dict_bytes{0};
};

Status ComputeRepoStats(const std::string& repo_path, RepoStats* out);

}  // namespace ebbackup
