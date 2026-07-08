#pragma once

#include <cstddef>
#include <cstdint>

#include "ebbackup/codec/codec_types.h"

namespace ebbackup {

enum class ContentDataClass;

struct TierZstdParams {
  int fast_class_level{1};
  int slow_class_level{1};
  bool enable_ldm{false};
  size_t ldm_min_bytes{128u * 1024u};
  double lz4_retry_ratio_threshold{0.92};
  bool prefer_lz4_for_fast{true};
};

TierZstdParams ResolveTierParams(CompressTier tier, int level_override);

int ResolveZstdLevel(ContentDataClass cls, const TierZstdParams& params);

bool ShouldEnableLdm(size_t chunk_len, const TierZstdParams& params);

uint32_t CpuBudgetZstdCost(size_t chunk_len, int level);

}  // namespace ebbackup
