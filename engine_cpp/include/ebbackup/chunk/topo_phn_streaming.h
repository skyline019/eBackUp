#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/chunk/topo_phn.h"
#include "ebbackup/chunk/topo_phn_internal.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

struct TopoPhnStreamProfile {
  uint64_t carry_copy_ns{0};
  uint64_t topo_scan_ns{0};
  uint64_t digest_ns{0};
  uint64_t topo_scan_probes{0};
};

struct TopoPhnStreamState {
  TopoPhnConfig config{};
  std::vector<uint8_t> carry;
  uint64_t logical_base{0};
  bool tables_ready{false};
  const uint8_t* digest_base{nullptr};
  TopoPhnStreamProfile profile{};
  topo_phn_internal::TopoPhnResume resume{};
};

inline void ResetTopoPhnStreamProfile(TopoPhnStreamProfile* profile) {
  if (!profile) return;
  profile->carry_copy_ns = 0;
  profile->topo_scan_ns = 0;
  profile->digest_ns = 0;
  profile->topo_scan_probes = 0;
}

void TopoPhnStreamInit(TopoPhnStreamState* state, TopoPhnConfig config);

Status TopoPhnStreamFeed(TopoPhnStreamState* state, const uint8_t* data,
                         size_t len, bool is_last,
                         std::vector<ChunkDescriptor>* out_chunks);

}  // namespace ebbackup
