#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/chunk/gt_cdc.h"
#include "ebbackup/chunk/gt_cdc_internal.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

struct GtCdcStreamProfile {
  uint64_t carry_copy_ns{0};
  uint64_t gtcdc_scan_ns{0};
  uint64_t digest_ns{0};
  uint64_t blocks_composed{0};
  uint64_t vector8_groups{0};
  uint64_t gtcdc_scan_probes{0};
};

struct GtCdcStreamState {
  GtCdcConfig config{};
  uint32_t alpha_pow[gtcdc_internal::kAlphaPowTableSize]{};
  std::vector<uint8_t> carry;
  uint64_t logical_base{0};
  size_t stream_offset{0};
  bool tables_ready{false};
  const uint8_t* digest_base{nullptr};
  GtCdcStreamProfile profile{};
  uint64_t an_chunk_abs_start{~0ULL};
  uint64_t an_scan_abs{~0ULL};
  uint32_t an_gear_h{0};
  bool an_gear_h_valid{false};
  uint64_t tf_scan_abs{~0ULL};
  uint32_t tf_gear_h{0};
  uint32_t tf_norm_h{0};
  bool tf_h_valid{false};
};

inline void ResetGtCdcStreamProfile(GtCdcStreamProfile* profile) {
  if (!profile) return;
  profile->carry_copy_ns = 0;
  profile->gtcdc_scan_ns = 0;
  profile->digest_ns = 0;
  profile->blocks_composed = 0;
  profile->vector8_groups = 0;
  profile->gtcdc_scan_probes = 0;
}

void AccumulateGtCdcStreamProfile(const GtCdcStreamProfile& src,
                                  GtCdcStreamProfile* dst);

void GtCdcStreamInit(GtCdcStreamState* state, GtCdcConfig config);

Status GtCdcStreamFeed(GtCdcStreamState* state, const uint8_t* data, size_t len,
                       bool is_last, std::vector<ChunkDescriptor>* out_chunks);

Status GtCdcStreamFinish(GtCdcStreamState* state,
                         std::vector<ChunkDescriptor>* out_chunks);

}  // namespace ebbackup
