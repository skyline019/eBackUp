#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/topo_phn.h"
#include "ebbackup/common/status.h"

namespace ebbackup {
namespace topo_phn_internal {

constexpr size_t kTopoPhnMaxK = 16;
constexpr size_t kPhnH0EpsCount = 3;
constexpr uint16_t kMinEventStride = 8;
// Runtime may exceed uint16 SB packing (Default-calib only is stored).
constexpr uint32_t kMaxEventStride = 2u * 1024u * 1024u;
constexpr size_t kPhnCalibSampleBytes = 4u * 1024u * 1024u;
// InitRepo calibrates under Default profile (avg=256KiB); runtime scales stride.
constexpr uint32_t kPhnCalibAvgRef = 256u * 1024u;

inline uint8_t ClampKPoints(uint8_t k) {
  if (k < 4) return 4;
  if (k > kTopoPhnMaxK) return static_cast<uint8_t>(kTopoPhnMaxK);
  return k;
}

inline uint32_t ClampEventStride(uint32_t s) {
  if (s < kMinEventStride) return kMinEventStride;
  if (s > kMaxEventStride) return kMaxEventStride;
  return s;
}

// Scan hot-path uses (y & (stride-1)) — keep stride a power of two.
// Nearest (ties → lower) avoids ceil blow-ups like 65537→131072 after sparsify.
inline uint32_t RoundEventStridePow2(uint32_t s) {
  s = ClampEventStride(s);
  if (s <= kMinEventStride) return kMinEventStride;
  uint32_t hi = kMinEventStride;
  while (hi < s) {
    if (hi > (kMaxEventStride >> 1)) return kMaxEventStride;
    hi <<= 1;
  }
  const uint32_t lo = hi >> 1;
  if (lo < kMinEventStride) return hi;
  return (s - lo <= hi - s) ? lo : hi;
}

// Soft floor for PH densify: avoid stride=8 death spiral until τ/k exhausted.
inline uint32_t PhnProductiveStrideFloor(uint32_t avg_size) {
  const uint32_t from_avg = avg_size > 1024u ? avg_size / 1024u : 32u;
  return RoundEventStridePow2(std::max(32u, from_avg));
}

// Map Default-calibrated SB stride onto the active profile avg_size.
inline uint32_t ScaleEventStrideForAvg(uint32_t sb_stride, uint32_t avg_size) {
  if (avg_size == 0 || avg_size == kPhnCalibAvgRef) {
    return RoundEventStridePow2(sb_stride);
  }
  const uint64_t scaled =
      (static_cast<uint64_t>(sb_stride) * static_cast<uint64_t>(avg_size) +
       (kPhnCalibAvgRef / 2u)) /
      kPhnCalibAvgRef;
  return RoundEventStridePow2(static_cast<uint32_t>(
      std::max<uint64_t>(scaled, kMinEventStride)));
}

void FillPhnCalibSample(uint8_t* out, size_t len, uint32_t seed);

// Content-local embed at absolute index p (bit-identical closed-form for p>=3).
uint32_t LandmarkEmbedY(const uint8_t* data, size_t len, size_t p,
                        uint32_t table_seed);

uint32_t FlipCountFixed(const uint32_t* recent_y, const uint32_t* recent_t,
                        uint32_t count, uint8_t k_points, uint32_t q_mod,
                        uint8_t ring_start = 0);

void PhH0BettiVec(const uint32_t* recent_y, const uint32_t* recent_t,
                  uint32_t count, uint8_t k_points, uint32_t q_mod,
                  uint32_t out_beta0[kPhnH0EpsCount], uint8_t ring_start = 0);

bool PhH0Delta(const uint32_t* prev, const uint32_t* cur);

uint8_t PhH0PersistSpan(const uint32_t* beta0);

uint32_t CalibrateEventStride(const uint8_t* sample, size_t sample_len,
                              const TopoPhnConfig& cfg);

// Mutates event_stride / flip_tau / persist knobs so mean ≈ avg.
// Only event_stride is persisted in the superblock; other knobs must be
// re-derived via CalibratePhnRuntimeParams when loading a repo.
void CalibratePhnCutParams(const uint8_t* sample, size_t sample_len,
                           TopoPhnConfig* cfg);

void CalibratePhnRuntimeParams(const uint8_t* sample, size_t sample_len,
                               TopoPhnConfig* cfg);

struct TopoPhnResume {
  size_t scan_rel{0};
  bool in_scan{false};
  uint32_t recent_y[kTopoPhnMaxK]{};
  uint32_t recent_t[kTopoPhnMaxK]{};
  uint32_t recent_count{0};
  uint8_t ring_start{0};  // index of oldest when window is full
  uint32_t prev_flip{0};
  uint32_t prev_beta0[kPhnH0EpsCount]{};
  bool has_prev{false};
  size_t bytes_since_lm{0};
  size_t last_cut_abs{0};
  bool has_last_cut{false};
  uint32_t rolling_mix{0};
};

void ClearTopoPhnResume(TopoPhnResume* resume);

bool ProcessPhnChunk(const uint8_t* data, size_t len, size_t chunk_pos,
                     bool allow_tail_cut, const TopoPhnConfig& cfg,
                     TopoPhnResume* resume, size_t* out_cut, bool* chunk_done,
                     uint64_t* probes = nullptr, size_t abs_base = 0);

Status CollectChunkCutsTopoPhn(const uint8_t* data, size_t len,
                               const TopoPhnConfig& cfg,
                               std::vector<size_t>* offsets,
                               std::vector<uint32_t>* lengths);

}  // namespace topo_phn_internal
}  // namespace ebbackup
