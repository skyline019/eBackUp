#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/chunk/topo_ph.h"
#include "ebbackup/chunk/topo_ph_internal.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

struct TopoPhStreamProfile {
  uint64_t carry_copy_ns{0};
  uint64_t topo_scan_ns{0};
  uint64_t digest_ns{0};
  uint64_t topo_scan_probes{0};
};

struct TopoPhStreamState {
  TopoPhConfig config{};
  uint32_t gear[256]{};
  std::vector<uint8_t> carry;
  uint64_t logical_base{0};
  bool tables_ready{false};
  const uint8_t* digest_base{nullptr};
  TopoPhStreamProfile profile{};
  topo_ph_internal::TopoPhResume resume{};
};

inline void ResetTopoPhStreamProfile(TopoPhStreamProfile* profile) {
  if (!profile) return;
  profile->carry_copy_ns = 0;
  profile->topo_scan_ns = 0;
  profile->digest_ns = 0;
  profile->topo_scan_probes = 0;
}

void TopoPhStreamInit(TopoPhStreamState* state, TopoPhConfig config);

Status TopoPhStreamFeed(TopoPhStreamState* state, const uint8_t* data, size_t len,
                        bool is_last, std::vector<ChunkDescriptor>* out_chunks);

}  // namespace ebbackup
