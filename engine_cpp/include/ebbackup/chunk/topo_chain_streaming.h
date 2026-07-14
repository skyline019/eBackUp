#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/chunk/topo_cdc.h"
#include "ebbackup/chunk/topo_chain_internal.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

struct TopoChainStreamProfile {
  uint64_t carry_copy_ns{0};
  uint64_t chain_scan_ns{0};
  uint64_t digest_ns{0};
  uint64_t chain_scan_probes{0};
};

struct TopoChainStreamState {
  TopoCdcConfig config{};
  std::vector<uint8_t> carry;
  uint64_t logical_base{0};
  const uint8_t* digest_base{nullptr};
  TopoChainStreamProfile profile{};
  topo_chain_internal::TopoChainResume chain_resume{};
};

inline void ResetTopoChainStreamProfile(TopoChainStreamProfile* profile) {
  if (!profile) return;
  profile->carry_copy_ns = 0;
  profile->chain_scan_ns = 0;
  profile->digest_ns = 0;
  profile->chain_scan_probes = 0;
}

void TopoChainStreamInit(TopoChainStreamState* state, TopoCdcConfig config);

Status TopoChainStreamFeed(TopoChainStreamState* state, const uint8_t* data,
                           size_t len, bool is_last,
                           std::vector<ChunkDescriptor>* out_chunks);

}  // namespace ebbackup
