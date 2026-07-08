#include "ebbackup/codec/compress_policy.h"

#include <algorithm>

#include "ebbackup/codec/content_class.h"

namespace ebbackup {

namespace {

TierZstdParams FastTierDefaults() {
  TierZstdParams p{};
  p.fast_class_level = 1;
  p.slow_class_level = 1;
  p.enable_ldm = false;
  p.lz4_retry_ratio_threshold = 0.92;
  p.prefer_lz4_for_fast = true;
  return p;
}

TierZstdParams BalancedTierDefaults() {
  TierZstdParams p{};
  p.fast_class_level = 3;
  p.slow_class_level = 6;
  p.enable_ldm = true;
  p.ldm_min_bytes = 128u * 1024u;
  p.lz4_retry_ratio_threshold = 0.90;
  p.prefer_lz4_for_fast = true;
  return p;
}

TierZstdParams MaxTierDefaults() {
  TierZstdParams p{};
  p.fast_class_level = 6;
  p.slow_class_level = 15;
  p.enable_ldm = true;
  p.ldm_min_bytes = 64u * 1024u;
  p.lz4_retry_ratio_threshold = 1.0;
  p.prefer_lz4_for_fast = false;
  return p;
}

}  // namespace

TierZstdParams ResolveTierParams(CompressTier tier, int level_override) {
  TierZstdParams params;
  switch (tier) {
    case CompressTier::kBalanced:
      params = BalancedTierDefaults();
      break;
    case CompressTier::kMax:
      params = MaxTierDefaults();
      break;
    case CompressTier::kFast:
    default:
      params = FastTierDefaults();
      break;
  }
  if (level_override > 0) {
    const int level = std::max(1, std::min(level_override, 22));
    params.fast_class_level = level;
    params.slow_class_level = level;
  }
  return params;
}

int ResolveZstdLevel(ContentDataClass cls, const TierZstdParams& params) {
  if (cls == ContentDataClass::kSlowCompressible) {
    return params.slow_class_level;
  }
  return params.fast_class_level;
}

bool ShouldEnableLdm(size_t chunk_len, const TierZstdParams& params) {
  return params.enable_ldm && chunk_len >= params.ldm_min_bytes;
}

uint32_t CpuBudgetZstdCost(size_t chunk_len, int level) {
  const uint32_t base =
      static_cast<uint32_t>((chunk_len * 50u) / (1024u * 1024u)) + 1u;
  const uint32_t lvl = static_cast<uint32_t>(std::max(1, level));
  return base * lvl;
}

}  // namespace ebbackup
