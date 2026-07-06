#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

struct FastCdcConfig {
  uint32_t min_size{64 * 1024};
  uint32_t avg_size{256 * 1024};
  uint32_t max_size{1024 * 1024};
  uint32_t window_size{64};
};

class FastCdcSlice {
 public:
  explicit FastCdcSlice(FastCdcConfig config = {});

  Status Chunk(const uint8_t* data, size_t len,
                 std::vector<ChunkDescriptor>* out) const;

  const FastCdcConfig& config() const { return config_; }

 private:
  uint32_t Mask() const;

  FastCdcConfig config_;
  uint32_t gear_[256];
};

}  // namespace ebbackup
