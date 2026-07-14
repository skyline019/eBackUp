#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/chunk/topo_cdc.h"
#include "ebbackup/chunk/topo_cdc_internal.h"
#include "ebbackup/chunk/topo_tri_internal.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

struct TopoCdcStreamProfile {
  uint64_t carry_copy_ns{0};
  uint64_t topo_scan_ns{0};
  uint64_t digest_ns{0};
  uint64_t topo_scan_probes{0};
};

struct TopoCdcStreamState {
  TopoCdcConfig config{};
  uint32_t gear[256]{};
  std::vector<uint8_t> carry;
  uint64_t logical_base{0};
  bool tables_ready{false};
  const uint8_t* digest_base{nullptr};
  TopoCdcStreamProfile profile{};
  topo_cdc_internal::TopoHomResume hom_resume{};
  topo_tri_internal::TopoTriResume tri_resume{};
};

inline void ResetTopoCdcStreamProfile(TopoCdcStreamProfile* profile) {
  if (!profile) return;
  profile->carry_copy_ns = 0;
  profile->topo_scan_ns = 0;
  profile->digest_ns = 0;
  profile->topo_scan_probes = 0;
}

void TopoCdcStreamInit(TopoCdcStreamState* state, TopoCdcConfig config);

Status TopoCdcStreamFeed(TopoCdcStreamState* state, const uint8_t* data,
                         size_t len, bool is_last,
                         std::vector<ChunkDescriptor>* out_chunks);

}  // namespace ebbackup
