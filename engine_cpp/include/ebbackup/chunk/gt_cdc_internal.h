#pragma once

#include <cstddef>
#include <cstdint>

#include "ebbackup/chunk/gt_cdc_view.h"

namespace ebbackup {

enum class GtCdcKernel;
struct GtCdcConfig;

namespace gtcdc_internal {

constexpr size_t kAlphaPowTableSize = 65;

uint32_t BuildMask(uint32_t avg_size);
uint32_t BuildNormMask(uint32_t avg_size, uint8_t nc_level);
void InitGearTable(uint32_t gear[256]);
void InitKeyedGearTable(uint32_t gear[256], uint32_t seed);
bool ScanGearCut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                 uint32_t w, uint32_t mask, const uint32_t gear[256],
                 size_t* out_cut, bool* found, uint64_t* probes = nullptr);
bool ScanGearCutNc(const uint8_t* data, size_t scan_start, size_t cut_limit,
                   uint32_t w, uint32_t mask, uint32_t norm_mask,
                   const uint32_t gear[256], size_t* out_cut, bool* found,
                   uint64_t* probes = nullptr);

void Build2FMasks(uint32_t avg_size, uint8_t nc_level, uint32_t* mask_lo,
                  uint32_t* mask_hi, uint32_t* norm_shift);
bool ScanGearCut2F(const uint8_t* data, size_t scan_start, size_t cut_limit,
                   uint32_t w, uint32_t mask_lo, uint32_t mask_hi,
                   uint32_t norm_shift, const uint32_t gear[256],
                   const uint32_t norm_gear[256], size_t* out_cut, bool* found,
                   uint32_t h_gear_resume = 0, uint32_t h_norm_resume = 0,
                   bool use_h_resume = false, uint32_t* h_gear_out = nullptr,
                   uint32_t* h_norm_out = nullptr, uint64_t* probes = nullptr);

uint32_t MixSeedHash(uint32_t h, uint32_t seed, size_t pos);
uint32_t SelectPhaseMask(size_t d, uint32_t avg_size, uint8_t nc_level);
bool ScanGearCutAn(const uint8_t* data, size_t scan_start, size_t cut_limit,
                   size_t chunk_start, size_t pos_bias, uint32_t w,
                   uint32_t avg_size, uint8_t nc_level, uint32_t seed,
                   const uint32_t gear[256], size_t* out_cut, bool* found,
                   uint32_t h_resume = 0, bool use_h_resume = false,
                   uint32_t* h_out = nullptr, uint64_t* probes = nullptr);

void InitBetaTable(uint32_t beta_table[256]);
void InitGearTableForConfig(GtCdcConfig* cfg);

void InitAlphaPowTable(uint32_t alpha, uint32_t pow[kAlphaPowTableSize]);

uint32_t RabinStep(uint32_t h, uint8_t byte, uint32_t alpha,
                   const uint32_t beta_table[256]);

uint32_t RabinFeedRange(uint32_t h, const uint8_t* data, size_t len,
                        uint32_t alpha, const uint32_t beta_table[256]);

uint32_t BlockFingerprint(const uint8_t* data, size_t len, uint32_t alpha,
                          const uint32_t beta_table[256]);

uint32_t BlockFingerprint64(const uint8_t* data, uint32_t alpha,
                            const uint32_t beta_table[256]);

uint32_t BlockFingerprintFast(const uint8_t* data, size_t len, uint32_t alpha,
                              const uint32_t beta_table[256]);

uint32_t ComposeBlockFast(uint32_t h, uint32_t block_fingerprint,
                          uint32_t alpha_pow_len);

uint32_t ComposeBlock(uint32_t h, const uint8_t* data, size_t len,
                      uint32_t alpha, const uint32_t beta_table[256]);

uint32_t FeedRangeCompose(const uint8_t* data, size_t len, uint32_t h,
                          uint32_t alpha, const uint32_t beta_table[256],
                          uint32_t block_B,
                          const uint32_t pow[kAlphaPowTableSize]);

uint32_t MaskForAvg(uint32_t avg_size);

struct GtCdcScanStats {
  uint64_t blocks_composed{0};
  uint64_t vector8_groups{0};
};

bool ScanRabinCutScalar(const uint8_t* data, size_t scan_start, size_t cut_limit,
                        uint32_t h_initial, uint32_t alpha,
                        const uint32_t beta_table[256], uint32_t mask,
                        size_t* out_cut, bool* found, uint32_t* h_out);

bool GtCdcScan(const uint8_t* data, size_t scan_start, size_t cut_limit,
               uint32_t h_initial, uint32_t alpha,
               const uint32_t beta_table[256], uint32_t mask, uint32_t block_B,
               const uint32_t pow[kAlphaPowTableSize], size_t* out_cut,
               bool* found, uint32_t* h_out, GtCdcScanStats* stats = nullptr);

bool GtCdcScanGear(const uint8_t* data, size_t scan_start, size_t cut_limit,
                   uint32_t window_w, uint32_t mask, const uint32_t gear[256],
                   size_t* out_cut, bool* found, uint64_t* probes = nullptr);

bool GtCdcScanGearNc(const uint8_t* data, size_t scan_start, size_t cut_limit,
                     uint32_t window_w, uint32_t mask, uint32_t norm_mask,
                     const uint32_t gear[256], size_t* out_cut, bool* found,
                     uint64_t* probes = nullptr);

bool GtCdcScanGearView(const GtCdcSegmentView& view, size_t scan_start,
                       size_t cut_limit, uint32_t window_w, uint32_t mask,
                       const uint32_t gear[256], size_t* out_cut, bool* found,
                       uint64_t* probes = nullptr);

bool GtCdcScanGearNcView(const GtCdcSegmentView& view, size_t scan_start,
                         size_t cut_limit, uint32_t window_w, uint32_t mask,
                         uint32_t norm_mask, const uint32_t gear[256],
                         size_t* out_cut, bool* found,
                         uint64_t* probes = nullptr);

bool GtCdcScanGearAn(const uint8_t* data, size_t scan_start, size_t cut_limit,
                     size_t chunk_start, size_t pos_bias, uint32_t window_w,
                     uint32_t avg_size, uint8_t nc_level, uint32_t seed,
                     const uint32_t gear[256], size_t* out_cut, bool* found);

bool GtCdcScanGearAnView(const GtCdcSegmentView& view, size_t scan_start,
                         size_t cut_limit, size_t chunk_start, size_t pos_bias,
                         uint32_t window_w, uint32_t avg_size,
                         uint8_t nc_level, uint32_t seed,
                         const uint32_t gear[256], size_t* out_cut,
                         bool* found);

bool GtCdcScanGear2F(const uint8_t* data, size_t scan_start, size_t cut_limit,
                     uint32_t window_w, uint32_t mask_lo, uint32_t mask_hi,
                     uint32_t norm_shift, const uint32_t gear[256],
                     const uint32_t norm_gear[256], size_t* out_cut,
                     bool* found, uint64_t* probes = nullptr);

bool GtCdcScanGear2FView(const GtCdcSegmentView& view, size_t scan_start,
                         size_t cut_limit, uint32_t window_w, uint32_t mask_lo,
                         uint32_t mask_hi, uint32_t norm_shift,
                         const uint32_t gear[256], const uint32_t norm_gear[256],
                         size_t* out_cut, bool* found,
                         uint32_t h_gear_resume = 0, uint32_t h_norm_resume = 0,
                         bool use_h_resume = false, uint32_t* h_gear_out = nullptr,
                         uint32_t* h_norm_out = nullptr,
                         uint64_t* probes = nullptr);

}  // namespace gtcdc_internal
}  // namespace ebbackup
