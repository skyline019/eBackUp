#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/topo_ph.h"
#include "ebbackup/common/status.h"

namespace ebbackup {
namespace topo_ph_internal {

constexpr size_t kTopoPhMaxK = 24;
constexpr size_t kPhH0EpsCount = 5;

inline uint8_t ClampKPoints(uint8_t k) {
  if (k < 4) return 4;
  if (k > kTopoPhMaxK) return static_cast<uint8_t>(kTopoPhMaxK);
  return k;
}

void InitGearTable(uint32_t gear[256], uint32_t seed);
void BuildPhMasks(uint32_t avg_size, uint16_t calib_permille, uint8_t topo_shift,
                  uint32_t* mask_out);
uint32_t InitWindowHash(const uint8_t* data, size_t end, uint32_t w,
                        const uint32_t gear[256]);

void FillTopoPhCalibSample(uint8_t* out, size_t len, uint32_t seed);
uint16_t CalibrateTopoPhPermille(const uint8_t* sample, size_t sample_len,
                                 const TopoPhConfig& cfg, uint32_t seed);

// Integer orientation / flip proxy (deterministic).
uint32_t FlipCountFixed(const uint32_t* recent_h, const uint32_t* recent_t,
                        uint32_t count, uint8_t k_points, uint32_t q_mod);

// VR-H0 Betti-0 at fixed R^2 scales (integer).
void PhH0BettiVec(const uint32_t* recent_h, const uint32_t* recent_t,
                  uint32_t count, uint8_t k_points, uint32_t q_mod,
                  uint32_t out_beta0[kPhH0EpsCount]);
bool PhH0Delta(const uint32_t* prev, const uint32_t* cur);

bool ScanTriV2Cut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                  const TopoPhConfig& cfg, uint32_t mask, const uint32_t gear[256],
                  size_t* out_cut, bool* found, uint64_t* probes = nullptr);

bool ScanPhH0Cut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                 const TopoPhConfig& cfg, uint32_t mask, const uint32_t gear[256],
                 size_t* out_cut, bool* found, uint64_t* probes = nullptr);

struct TopoPhResume {
  uint32_t h{0};
  size_t scan_rel{0};
  bool in_scan{false};
  uint32_t window_w{64};
  uint32_t recent_h[kTopoPhMaxK]{};
  uint32_t recent_t[kTopoPhMaxK]{};
  uint32_t recent_count{0};
  uint32_t prev_flip{0};
  uint32_t prev_beta0[kPhH0EpsCount]{};
  bool has_prev{false};
};

void ClearTopoPhResume(TopoPhResume* resume);

bool ProcessTopoPhChunk(const uint8_t* data, size_t len, size_t chunk_pos,
                        bool allow_tail_cut, const TopoPhConfig& cfg,
                        uint32_t mask, const uint32_t gear[256],
                        TopoPhResume* resume, size_t* out_cut, bool* chunk_done,
                        uint64_t* probes = nullptr);

Status CollectChunkCutsTopoPh(const uint8_t* data, size_t len,
                              const TopoPhConfig& cfg, const uint32_t gear[256],
                              std::vector<size_t>* offsets,
                              std::vector<uint32_t>* lengths);

}  // namespace topo_ph_internal
}  // namespace ebbackup
