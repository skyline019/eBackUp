#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/chunk/gt_cdc_internal.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

constexpr uint32_t kFastCdcAlgoId = 0;
constexpr uint32_t kGtCdcAlgoId = 1;

enum class GtCdcKernel { kRabin = 0, kGear = 1, kNative = 2, kAnGear = 3, kTwoFGear = 4 };

struct GtCdcConfig {
  uint32_t min_size{64 * 1024};
  uint32_t avg_size{256 * 1024};
  uint32_t max_size{1024 * 1024};
  uint32_t window_w{64};
  uint32_t block_B{64};
  uint32_t alpha{0x00010001u};
  uint32_t beta_table[256]{};
  uint32_t norm_table[256]{};
  uint32_t table_seed{0};
  uint8_t nc_level{0};
  DigestAlgo digest_algo{DigestAlgo::kLegacy};
  GtCdcKernel kernel{GtCdcKernel::kRabin};
};

class GtCdcSlice {
 public:
  explicit GtCdcSlice(GtCdcConfig config = {});

  Status Chunk(const uint8_t* data, size_t len,
               std::vector<ChunkDescriptor>* out) const;

  Status ChunkCuts(const uint8_t* data, size_t len, std::vector<size_t>* offsets,
                   std::vector<uint32_t>* lengths) const;

  Status ChunkCutsScalar(const uint8_t* data, size_t len,
                         std::vector<size_t>* offsets,
                         std::vector<uint32_t>* lengths) const;

  const GtCdcConfig& config() const { return config_; }
  const uint32_t* alpha_pow() const { return alpha_pow_; }

 private:
  uint32_t Mask() const;

  GtCdcConfig config_;
  uint32_t alpha_pow_[gtcdc_internal::kAlphaPowTableSize]{};
};

bool CdcGtCdcEnabled();

inline bool GtCdcUsesGearFamily(GtCdcKernel kernel) {
  return kernel == GtCdcKernel::kGear || kernel == GtCdcKernel::kNative ||
         kernel == GtCdcKernel::kAnGear || kernel == GtCdcKernel::kTwoFGear;
}

}  // namespace ebbackup
