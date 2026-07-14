#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

struct FastCdcStreamProfile {
  uint64_t carry_copy_ns{0};
  uint64_t cdc_scan_ns{0};
  uint64_t digest_ns{0};
  uint64_t cdc_scan_probes{0};
};

struct FastCdcStreamState {
  FastCdcConfig config{};
  uint32_t gear[256]{};
  std::vector<uint8_t> carry;
  uint64_t logical_base{0};
  size_t stream_offset{0};
  bool gear_ready{false};
  const uint8_t* digest_base{nullptr};
  FastCdcStreamProfile profile{};
};

inline void ResetFastCdcStreamProfile(FastCdcStreamProfile* profile) {
  if (!profile) return;
  profile->carry_copy_ns = 0;
  profile->cdc_scan_ns = 0;
  profile->digest_ns = 0;
  profile->cdc_scan_probes = 0;
}

void AccumulateFastCdcStreamProfile(const FastCdcStreamProfile& src,
                                    FastCdcStreamProfile* dst);

void FastCdcStreamInit(FastCdcStreamState* state, FastCdcConfig config);

Status FastCdcStreamFeed(FastCdcStreamState* state, const uint8_t* data,
                         size_t len, bool is_last,
                         std::vector<ChunkDescriptor>* out_chunks);

Status FastCdcStreamFinish(FastCdcStreamState* state,
                           std::vector<ChunkDescriptor>* out_chunks);

}  // namespace ebbackup
