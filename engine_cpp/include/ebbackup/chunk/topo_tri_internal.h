#pragma once

#include <cstddef>
#include <cstdint>

#include "ebbackup/chunk/topo_cdc.h"

namespace ebbackup {
namespace topo_tri_internal {

constexpr size_t kTriMaxKPoints = 32;

uint32_t DelaunayFlipCount(const uint32_t* recent_h, const uint32_t* recent_t,
                           uint32_t count, uint8_t k_points, uint32_t q_mod);

bool ScanTriCut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                const TopoCdcConfig& cfg, uint32_t mask,
                const uint32_t gear[256], size_t* out_cut, bool* found,
                uint64_t* probes = nullptr);

struct TopoTriResume {
  uint32_t h{0};
  size_t scan_rel{0};
  bool in_scan{false};
  uint32_t window_w{64};
  uint32_t recent_h[kTriMaxKPoints]{};
  uint32_t recent_t[kTriMaxKPoints]{};
  uint32_t recent_count{0};
};

void ClearTopoTriResume(TopoTriResume* resume);

bool ProcessTriChunk(const uint8_t* data, size_t len, size_t chunk_pos,
                     bool allow_tail_cut, const TopoCdcConfig& cfg,
                     uint32_t mask, const uint32_t gear[256],
                     TopoTriResume* resume, size_t* out_cut, bool* chunk_done,
                     uint64_t* probes = nullptr);

}  // namespace topo_tri_internal
}  // namespace ebbackup
