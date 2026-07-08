#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

struct FastCdcConfig {
  uint32_t min_size{64 * 1024};
  uint32_t avg_size{256 * 1024};
  uint32_t max_size{1024 * 1024};
  uint32_t window_size{64};
  DigestAlgo digest_algo{DigestAlgo::kLegacy};
};

struct FastCdcCutCursor {
  size_t pos{0};
};

class FastCdcSlice {
 public:
  explicit FastCdcSlice(FastCdcConfig config = {});

  Status Chunk(const uint8_t* data, size_t len,
               std::vector<ChunkDescriptor>* out) const;

  Status ChunkCuts(const uint8_t* data, size_t len, std::vector<size_t>* offsets,
                   std::vector<uint32_t>* lengths) const;

  // Resumable cuts for feed-timed replay: emit cuts with offset < until_offset.
  Status ChunkCutsUntil(const uint8_t* data, size_t len, size_t until_offset,
                        FastCdcCutCursor* cursor, std::vector<size_t>* offsets,
                        std::vector<uint32_t>* lengths, bool* complete) const;

  const FastCdcConfig& config() const { return config_; }

 private:
  uint32_t Mask() const;

  FastCdcConfig config_;
  uint32_t gear_[256];
};

}  // namespace ebbackup
