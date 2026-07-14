#pragma once

#include <cstddef>
#include <cstdint>

#include "ebbackup/chunk/topo_cdc.h"

namespace ebbackup {
namespace topo_chain_internal {

constexpr size_t kChainMaxWindow = 128;

uint32_t Rotl32(uint32_t v, uint32_t bits);

uint8_t ChainQuantKey(uint8_t byte, uint8_t quant_q);

uint32_t InitChainSignature(const uint8_t* data, size_t end, uint32_t w,
                            uint8_t quant_q, uint32_t lfsr_seed);

uint32_t UpdateChainSignature(uint32_t s, uint8_t byte, uint8_t quant_q);

uint32_t ChainSignatureAt(const uint8_t* data, size_t scan_start, size_t pos,
                          uint32_t w, uint8_t quant_q, uint32_t lfsr_seed);

uint32_t ChainStrideMask(uint8_t stride_log);

int32_t ChainPrimaryDelta(const uint8_t* keys, uint32_t w, uint8_t key_in,
                          uint32_t ed_old);

// Approximate β̂₁ over GF(2) on undirected 1-skeleton of keyed window:
// max(0, E - V + C). Returns the approximate dimension.
uint32_t ChainBeta1Gf2(const uint8_t* keys, uint32_t w);

// Reference (slow) implementation — bit-identical to ChainBeta1Gf2, for tests.
uint32_t ChainBeta1Gf2Reference(const uint8_t* keys, uint32_t w);

uint32_t SyncEdgeDiffFromKeys(const uint8_t* keys, uint32_t w);

void FillChainCalibSample(uint8_t* out, size_t len, uint32_t seed);

uint16_t CalibrateChainStrideLog(const uint8_t* sample, size_t sample_len,
                                 const TopoCdcConfig& cfg);

bool ScanChainCut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                  const TopoCdcConfig& cfg, size_t* out_cut, bool* found,
                  uint64_t* probes = nullptr);

struct TopoChainResume {
  uint32_t S{0};
  size_t scan_rel{0};
  bool in_scan{false};
  bool window_ready{false};
  uint32_t window_w{64};
  uint32_t edge_diff{0};
  uint8_t keys[kChainMaxWindow]{};
};

void ClearTopoChainResume(TopoChainResume* resume);

bool ProcessChainChunk(const uint8_t* data, size_t len, size_t chunk_pos,
                       bool allow_tail_cut, const TopoCdcConfig& cfg,
                       TopoChainResume* resume, size_t* out_cut,
                       bool* chunk_done, uint64_t* probes = nullptr);

bool ChainParallelScanEnabled();

bool RunChainScanLoopDispatch(const uint8_t* data, size_t p_start,
                              size_t cut_limit, uint32_t w,
                              uint32_t stride_mask, uint8_t quant_q,
                              bool enable_beta1, uint32_t lfsr_seed,
                              uint32_t* s, uint8_t* keys, uint32_t* edge_diff,
                              bool force_serial, size_t* out_cut,
                              uint64_t* probes = nullptr);

}  // namespace topo_chain_internal
}  // namespace ebbackup
