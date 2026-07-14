#pragma once

#include <cstddef>
#include <cstdint>

#include "ebbackup/chunk/topo_cdc.h"

namespace ebbackup {
namespace topo_cdc_internal {

constexpr size_t kTopoMaxWindow = 128;

void InitGearTable(uint32_t gear[256], uint32_t seed);

void BuildTopoMasks(uint32_t avg_size, uint16_t calib_permille, uint8_t topo_shift,
                    uint32_t* mask_out);

void FillTopoCalibSample(uint8_t* out, size_t len, uint32_t seed);

uint16_t CalibrateTopoPermille(const uint8_t* sample, size_t sample_len,
                               const TopoCdcConfig& cfg, uint32_t seed);

uint32_t InitWindowHash(const uint8_t* data, size_t end, uint32_t w,
                        const uint32_t gear[256]);

struct SlotUfWindow {
  uint32_t w{64};
  uint32_t wmask{63};
  uint32_t head{0};
  uint32_t filled{0};
  uint32_t components{0};
  uint32_t edge_diff{0};
  uint8_t key[kTopoMaxWindow]{};
  uint8_t parent[kTopoMaxWindow]{};
  uint8_t rank[kTopoMaxWindow]{};

  void Reset(uint32_t window_w);
  void LoadWindow(const uint8_t* keys, size_t count);
  int32_t Slide(uint8_t key_in);
  int32_t SlideViaRebuild(uint8_t key_in);
  uint32_t ComponentCount() const { return components; }
  void RecountEdgeDiff();

 private:
  void RebuildComponents();
  void SyncEdgeDiffFromKeys();
};

uint32_t ApplySlideEdgeDiffO1(const SlotUfWindow& uf, uint8_t key_in,
                              uint32_t head_old, uint8_t old_tail_key);

uint32_t ComponentsAfterSlide(uint32_t edge_diff, uint8_t key_in,
                              uint8_t head_old_key, uint32_t w);

bool ScanHomCut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                uint32_t w, uint32_t mask, const uint32_t gear[256],
                size_t* out_cut, bool* found, uint64_t* probes = nullptr);

struct TopoHomResume {
  uint32_t h{0};
  size_t scan_rel{0};
  bool in_scan{false};
  bool uf_ready{false};
  SlotUfWindow uf{};
  uint32_t window_w{64};
};

void ClearTopoHomResume(TopoHomResume* resume);

bool ProcessHomChunk(const uint8_t* data, size_t len, size_t chunk_pos,
                     bool allow_tail_cut, const TopoCdcConfig& cfg,
                     uint32_t mask, const uint32_t gear[256],
                     TopoHomResume* resume, size_t* out_cut, bool* chunk_done,
                     uint64_t* probes = nullptr);

}  // namespace topo_cdc_internal
}  // namespace ebbackup
