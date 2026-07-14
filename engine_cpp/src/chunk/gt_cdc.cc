#include "ebbackup/chunk/gt_cdc.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <numeric>
#include <vector>

#if defined(__AVX2__) || defined(_M_AVX2)
#define EBBACKUP_GTCDC_HAVE_AVX2 1
#else
#define EBBACKUP_GTCDC_HAVE_AVX2 0
#endif

#include "ebbackup/chunk/gt_cdc_internal.h"
#include "ebbackup/common/digest_pool.h"

namespace ebbackup {

namespace {

constexpr size_t kParallelHashMinBytes = 1024 * 1024;
constexpr size_t kParallelHashMinChunks = 2;

size_t NextChunkCutEndGear(const uint8_t* data, size_t len, size_t pos,
                           const GtCdcConfig& cfg, uint32_t mask,
                           size_t* out_cut) {
  const size_t remaining = len - pos;
  if (remaining <= cfg.min_size) return len;

  const uint32_t w = cfg.window_w;
  const size_t scan_start = pos + cfg.min_size;
  size_t cut = std::min(pos + cfg.max_size, len);
  bool found = false;

  if (scan_start >= w && scan_start < cut) {
    gtcdc_internal::GtCdcScanGear(data, scan_start, cut, w, mask, cfg.beta_table,
                                  &cut, &found);
  }

  if (!found && remaining > cfg.max_size) {
    cut = pos + cfg.max_size;
  } else if (!found) {
    cut = len;
  }
  if (out_cut) *out_cut = cut;
  return cut;
}

size_t NextChunkCutEndNative(const uint8_t* data, size_t len, size_t pos,
                             const GtCdcConfig& cfg, uint32_t mask,
                             uint32_t norm_mask, size_t* out_cut) {
  const size_t remaining = len - pos;
  if (remaining <= cfg.min_size) return len;

  const uint32_t w = cfg.window_w;
  const size_t scan_start = pos + cfg.min_size;
  size_t cut = std::min(pos + cfg.max_size, len);
  bool found = false;

  if (scan_start >= w && scan_start < cut) {
    gtcdc_internal::GtCdcScanGearNc(data, scan_start, cut, w, mask, norm_mask,
                                   cfg.beta_table, &cut, &found);
  }

  if (!found && remaining > cfg.max_size) {
    cut = pos + cfg.max_size;
  } else if (!found) {
    cut = len;
  }
  if (out_cut) *out_cut = cut;
  return cut;
}

size_t NextChunkCutEnd2F(const uint8_t* data, size_t len, size_t pos,
                         const GtCdcConfig& cfg, size_t* out_cut) {
  const size_t remaining = len - pos;
  if (remaining <= cfg.min_size) return len;

  const uint32_t w = cfg.window_w;
  const size_t scan_start = pos + cfg.min_size;
  size_t cut = std::min(pos + cfg.max_size, len);
  bool found = false;
  uint32_t mask_lo = 0;
  uint32_t mask_hi = 0;
  uint32_t norm_shift = 0;
  gtcdc_internal::Build2FMasks(cfg.avg_size, cfg.nc_level, &mask_lo, &mask_hi,
                               &norm_shift);

  if (scan_start >= w && scan_start < cut) {
    gtcdc_internal::GtCdcScanGear2F(data, scan_start, cut, w, mask_lo, mask_hi,
                                    norm_shift, cfg.beta_table, cfg.norm_table,
                                    &cut, &found);
  }

  if (!found && remaining > cfg.max_size) {
    cut = pos + cfg.max_size;
  } else if (!found) {
    cut = len;
  }
  if (out_cut) *out_cut = cut;
  return cut;
}

size_t NextChunkCutEndAnGear(const uint8_t* data, size_t len, size_t pos,
                             const GtCdcConfig& cfg, size_t* out_cut) {
  const size_t remaining = len - pos;
  if (remaining <= cfg.min_size) return len;

  const uint32_t w = cfg.window_w;
  const size_t scan_start = pos + cfg.min_size;
  size_t cut = std::min(pos + cfg.max_size, len);
  bool found = false;

  if (scan_start >= w && scan_start < cut) {
    gtcdc_internal::GtCdcScanGearAn(data, scan_start, cut, pos, 0, w,
                                    cfg.avg_size, cfg.nc_level, cfg.table_seed,
                                    cfg.beta_table, &cut, &found);
  }

  if (!found && remaining > cfg.max_size) {
    cut = pos + cfg.max_size;
  } else if (!found) {
    cut = len;
  }
  if (out_cut) *out_cut = cut;
  return cut;
}

size_t NextChunkCutEnd(const uint8_t* data, size_t len, size_t pos,
                       const GtCdcConfig& cfg, uint32_t mask,
                       const uint32_t pow[gtcdc_internal::kAlphaPowTableSize],
                       bool use_scalar, size_t* out_cut) {
  const size_t remaining = len - pos;
  if (remaining <= cfg.min_size) return len;

  const size_t scan_start = pos + cfg.min_size;
  size_t cut = std::min(pos + cfg.max_size, len);
  bool found = false;
  uint32_t h = gtcdc_internal::FeedRangeCompose(
      data + pos, cfg.min_size, 0, cfg.alpha, cfg.beta_table, cfg.block_B, pow);

  if (scan_start < cut) {
    uint32_t h_out = h;
    gtcdc_internal::GtCdcScanStats scan_stats{};
    if (use_scalar) {
      gtcdc_internal::ScanRabinCutScalar(data, scan_start, cut, h, cfg.alpha,
                                         cfg.beta_table, mask, &cut, &found,
                                         &h_out);
    } else {
      gtcdc_internal::GtCdcScan(data, scan_start, cut, h, cfg.alpha,
                                cfg.beta_table, mask, cfg.block_B, pow, &cut,
                                &found, &h_out, &scan_stats);
    }
    (void)scan_stats;
  }

  if (!found && remaining > cfg.max_size) {
    cut = pos + cfg.max_size;
  } else if (!found) {
    cut = len;
  }
  if (out_cut) *out_cut = cut;
  return cut;
}

void HashChunkRegions(DigestAlgo algo, const uint8_t* data,
                      const std::vector<size_t>& offsets,
                      const std::vector<uint32_t>& lengths,
                      std::vector<ChunkDescriptor>* out) {
  out->clear();
  out->resize(offsets.size());
  const size_t count = offsets.size();
  if (count == 0) return;

  const bool use_pool =
      data && count >= kParallelHashMinChunks &&
      std::accumulate(lengths.begin(), lengths.end(), size_t{0}) >=
          kParallelHashMinBytes;

  if (use_pool) {
    std::vector<DigestSpan> spans(count);
    for (size_t i = 0; i < count; ++i) {
      spans[i].offset = offsets[i];
      spans[i].length = lengths[i];
    }
    std::vector<uint8_t> hashes(count * 32);
    DigestPool::Shared().HashRegions(algo, data, spans.data(), count,
                                     hashes.data());
    for (size_t i = 0; i < count; ++i) {
      (*out)[i].offset = offsets[i];
      (*out)[i].length = lengths[i];
      std::memcpy((*out)[i].hash, hashes.data() + i * 32, 32);
    }
    return;
  }

  for (size_t i = 0; i < count; ++i) {
    (*out)[i].offset = offsets[i];
    (*out)[i].length = lengths[i];
    ContentHash(algo, data + offsets[i], lengths[i], (*out)[i].hash);
  }
}

Status CollectChunkCuts(const uint8_t* data, size_t len, const GtCdcConfig& cfg,
                        const uint32_t pow[gtcdc_internal::kAlphaPowTableSize],
                        bool use_scalar, std::vector<size_t>* offsets,
                        std::vector<uint32_t>* lengths) {
  if (!offsets || !lengths) return Status::InvalidArgument("out is null");
  offsets->clear();
  lengths->clear();
  if (len == 0) return Status::Ok();

  const uint32_t mask = gtcdc_internal::MaskForAvg(cfg.avg_size);
  const uint32_t norm_mask =
      cfg.kernel == GtCdcKernel::kNative
          ? gtcdc_internal::BuildNormMask(cfg.avg_size, cfg.nc_level)
          : 0;
  size_t pos = 0;
  while (pos < len) {
    const size_t remaining = len - pos;
    if (remaining <= cfg.min_size) {
      offsets->push_back(pos);
      lengths->push_back(static_cast<uint32_t>(remaining));
      break;
    }
    size_t cut = 0;
    if (cfg.kernel == GtCdcKernel::kTwoFGear) {
      NextChunkCutEnd2F(data, len, pos, cfg, &cut);
    } else if (cfg.kernel == GtCdcKernel::kAnGear) {
      NextChunkCutEndAnGear(data, len, pos, cfg, &cut);
    } else if (cfg.kernel == GtCdcKernel::kNative) {
      NextChunkCutEndNative(data, len, pos, cfg, mask, norm_mask, &cut);
    } else if (cfg.kernel == GtCdcKernel::kGear) {
      NextChunkCutEndGear(data, len, pos, cfg, mask, &cut);
    } else {
      NextChunkCutEnd(data, len, pos, cfg, mask, pow, use_scalar, &cut);
    }
    offsets->push_back(pos);
    lengths->push_back(static_cast<uint32_t>(cut - pos));
    pos = cut;
  }
  return Status::Ok();
}

Status CollectChunkCutsRabin(const uint8_t* data, size_t len, const GtCdcConfig& cfg,
                             const uint32_t pow[gtcdc_internal::kAlphaPowTableSize],
                             bool use_scalar, std::vector<size_t>* offsets,
                             std::vector<uint32_t>* lengths) {
  if (!offsets || !lengths) return Status::InvalidArgument("out is null");
  offsets->clear();
  lengths->clear();
  if (len == 0) return Status::Ok();

  const uint32_t mask = gtcdc_internal::MaskForAvg(cfg.avg_size);
  size_t pos = 0;
  while (pos < len) {
    const size_t remaining = len - pos;
    if (remaining <= cfg.min_size) {
      offsets->push_back(pos);
      lengths->push_back(static_cast<uint32_t>(remaining));
      break;
    }
    size_t cut = 0;
    NextChunkCutEnd(data, len, pos, cfg, mask, pow, use_scalar, &cut);
    offsets->push_back(pos);
    lengths->push_back(static_cast<uint32_t>(cut - pos));
    pos = cut;
  }
  return Status::Ok();
}

}  // namespace

bool CdcGtCdcEnabled() {
  const char* env = std::getenv("EBBACKUP_CDC_ALGO");
  return env && std::strcmp(env, "gtcdc") == 0;
}

GtCdcSlice::GtCdcSlice(GtCdcConfig config) : config_(config) {
  gtcdc_internal::InitGearTableForConfig(&config_);
  gtcdc_internal::InitAlphaPowTable(config_.alpha, alpha_pow_);
}

uint32_t GtCdcSlice::Mask() const {
  return gtcdc_internal::MaskForAvg(config_.avg_size);
}

Status GtCdcSlice::ChunkCutsScalar(const uint8_t* data, size_t len,
                                   std::vector<size_t>* offsets,
                                   std::vector<uint32_t>* lengths) const {
  return CollectChunkCutsRabin(data, len, config_, alpha_pow_, true, offsets,
                               lengths);
}

Status GtCdcSlice::ChunkCuts(const uint8_t* data, size_t len,
                             std::vector<size_t>* offsets,
                             std::vector<uint32_t>* lengths) const {
#if !EBBACKUP_GTCDC_HAVE_AVX2
  (void)data;
  (void)len;
  (void)offsets;
  (void)lengths;
  return Status::Internal("G-TCDC requires AVX2; no fallback");
#else
  if (config_.kernel == GtCdcKernel::kGear ||
      config_.kernel == GtCdcKernel::kNative ||
      config_.kernel == GtCdcKernel::kAnGear ||
      config_.kernel == GtCdcKernel::kTwoFGear) {
    return CollectChunkCuts(data, len, config_, alpha_pow_, false, offsets,
                            lengths);
  }
  return CollectChunkCutsRabin(data, len, config_, alpha_pow_, false, offsets,
                               lengths);
#endif
}

Status GtCdcSlice::Chunk(const uint8_t* data, size_t len,
                         std::vector<ChunkDescriptor>* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  if (len == 0) return Status::Ok();

  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  const Status cuts_st = ChunkCuts(data, len, &offsets, &lengths);
  if (!cuts_st.ok()) return cuts_st;

  if (len <= config_.min_size) {
    ChunkDescriptor desc{};
    desc.offset = 0;
    desc.length = static_cast<uint32_t>(len);
    ContentHash(config_.digest_algo, data, len, desc.hash);
    out->push_back(desc);
    return Status::Ok();
  }

  HashChunkRegions(config_.digest_algo, data, offsets, lengths, out);
  return Status::Ok();
}

namespace gtcdc_internal {

void InitGearTableForConfig(GtCdcConfig* cfg) {
  if (!cfg) return;
  if (cfg->kernel == GtCdcKernel::kNative && cfg->table_seed != 0) {
    InitKeyedGearTable(cfg->beta_table, cfg->table_seed);
  } else {
    InitGearTable(cfg->beta_table);
  }
  if (cfg->kernel == GtCdcKernel::kTwoFGear) {
    const uint32_t norm_seed =
        cfg->table_seed != 0 ? (cfg->table_seed ^ 0x9E3779B9u) : 0u;
    if (norm_seed != 0) {
      InitKeyedGearTable(cfg->norm_table, norm_seed);
    } else {
      InitGearTable(cfg->norm_table);
    }
  }
}

void InitBetaTable(uint32_t beta_table[256]) {
  InitGearTable(beta_table);
}

void InitAlphaPowTable(uint32_t alpha, uint32_t pow[kAlphaPowTableSize]) {
  pow[0] = 1u;
  for (size_t i = 1; i < kAlphaPowTableSize; ++i) {
    pow[i] = alpha * pow[i - 1];
  }
}

uint32_t RabinStep(uint32_t h, uint8_t byte, uint32_t alpha,
                   const uint32_t beta_table[256]) {
  return alpha * h + beta_table[byte];
}

uint32_t RabinFeedRange(uint32_t h, const uint8_t* data, size_t len,
                        uint32_t alpha, const uint32_t beta_table[256]) {
  for (size_t i = 0; i < len; ++i) {
    h = RabinStep(h, data[i], alpha, beta_table);
  }
  return h;
}

uint32_t BlockFingerprint(const uint8_t* data, size_t len, uint32_t alpha,
                          const uint32_t beta_table[256]) {
  return RabinFeedRange(0, data, len, alpha, beta_table);
}

uint32_t FeedRangeCompose(const uint8_t* data, size_t len, uint32_t h,
                          uint32_t alpha, const uint32_t beta_table[256],
                          uint32_t block_B,
                          const uint32_t pow[kAlphaPowTableSize]) {
  if (len == 0) return h;
  if (block_B == 0) block_B = 64;
  size_t pos = 0;
  while (pos < len) {
    const size_t step = std::min(len - pos, static_cast<size_t>(block_B));
    const uint32_t fp = BlockFingerprintFast(data + pos, step, alpha, beta_table);
    h = ComposeBlockFast(h, fp, pow[step]);
    pos += step;
  }
  return h;
}

uint32_t ComposeBlockFast(uint32_t h, uint32_t block_fingerprint,
                          uint32_t alpha_pow_len) {
  return alpha_pow_len * h + block_fingerprint;
}

uint32_t ComposeBlock(uint32_t h, const uint8_t* data, size_t len,
                      uint32_t alpha, const uint32_t beta_table[256]) {
  return RabinFeedRange(h, data, len, alpha, beta_table);
}

uint32_t MaskForAvg(uint32_t avg_size) {
  return BuildMask(avg_size);
}

bool ScanRabinCutScalar(const uint8_t* data, size_t scan_start, size_t cut_limit,
                        uint32_t h_initial, uint32_t alpha,
                        const uint32_t beta_table[256], uint32_t mask,
                        size_t* out_cut, bool* found, uint32_t* h_out) {
  if (!data || !out_cut || !found) return false;
  *found = false;
  if (scan_start >= cut_limit) {
    if (h_out) *h_out = h_initial;
    return false;
  }

  uint32_t h = h_initial;
  for (size_t i = scan_start; i < cut_limit; ++i) {
    if ((h & mask) == 0) {
      *out_cut = i;
      *found = true;
      if (h_out) *h_out = h;
      return true;
    }
    h = RabinStep(h, data[i], alpha, beta_table);
  }
  if (h_out) *h_out = h;
  return false;
}

}  // namespace gtcdc_internal

}  // namespace ebbackup
