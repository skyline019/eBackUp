#include "ebbackup/chunk/gt_cdc_internal.h"
#include "ebbackup/chunk/gt_cdc_view.h"

#if defined(__AVX2__) || defined(_M_AVX2)
#include <immintrin.h>
#endif

namespace ebbackup {
namespace gtcdc_internal {

namespace {

uint64_t SplitMix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

uint32_t Rotl32(uint32_t v, uint32_t bits) {
  bits &= 31u;
  if (bits == 0) return v;
  return (v << bits) | (v >> (32u - bits));
}

uint32_t InitWindowHash(const uint8_t* data, size_t end, uint32_t w,
                        const uint32_t gear[256]) {
  uint32_t h = 0;
  const size_t start = end - w;
  for (size_t i = start; i < end; ++i) {
    h = (h << 1) + gear[data[i]];
  }
  return h;
}

bool ScanGearCutScalar(const uint8_t* data, size_t scan_start, size_t cut_limit,
                       uint32_t w, uint32_t mask, const uint32_t gear[256],
                       size_t* out_cut, bool* found, uint64_t* probes) {
  uint32_t h = InitWindowHash(data, scan_start, w, gear);
  size_t i = scan_start;
  for (; i + 8 <= cut_limit; i += 8) {
    for (size_t j = 0; j < 8; ++j) {
      const size_t pos = i + j;
      if (probes) ++*probes;
      if ((h & mask) == 0) {
        *out_cut = pos;
        *found = true;
        return true;
      }
      h = (h << 1) + gear[data[pos]] - gear[data[pos - w]];
    }
  }
  for (; i < cut_limit; ++i) {
    if (probes) ++*probes;
    if ((h & mask) == 0) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h = (h << 1) + gear[data[i]] - gear[data[i - w]];
  }
  return false;
}

bool ScanGearCutNcScalar(const uint8_t* data, size_t scan_start,
                         size_t cut_limit, uint32_t w, uint32_t mask,
                         uint32_t norm_mask, const uint32_t gear[256],
                         size_t* out_cut, bool* found, uint64_t* probes) {
  uint32_t h = InitWindowHash(data, scan_start, w, gear);
  size_t i = scan_start;
  for (; i + 8 <= cut_limit; i += 8) {
    for (size_t j = 0; j < 8; ++j) {
      const size_t pos = i + j;
      if (probes) ++*probes;
      if ((h & mask) == 0 && (h & norm_mask) == norm_mask) {
        *out_cut = pos;
        *found = true;
        return true;
      }
      h = (h << 1) + gear[data[pos]] - gear[data[pos - w]];
    }
  }
  for (; i < cut_limit; ++i) {
    if (probes) ++*probes;
    if ((h & mask) == 0 && (h & norm_mask) == norm_mask) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h = (h << 1) + gear[data[i]] - gear[data[i - w]];
  }
  return false;
}

bool ScanGearCut2FScalar(const uint8_t* data, size_t scan_start,
                         size_t cut_limit, uint32_t w, uint32_t mask_lo,
                         uint32_t mask_hi, uint32_t norm_shift,
                         const uint32_t gear[256], const uint32_t norm_gear[256],
                         size_t* out_cut, bool* found, uint32_t h_gear_resume,
                         uint32_t h_norm_resume, bool use_h_resume,
                         uint32_t* h_gear_out, uint32_t* h_norm_out,
                         uint64_t* probes) {
  uint32_t h_gear =
      use_h_resume ? h_gear_resume : InitWindowHash(data, scan_start, w, gear);
  uint32_t h_norm = use_h_resume
                        ? h_norm_resume
                        : InitWindowHash(data, scan_start, w, norm_gear);
  size_t i = scan_start;
  for (; i + 8 <= cut_limit; i += 8) {
    for (size_t j = 0; j < 8; ++j) {
      const size_t pos = i + j;
      if (probes) ++*probes;
      if ((h_gear & mask_lo) == 0 &&
          ((h_norm >> norm_shift) & mask_hi) == 0) {
        *out_cut = pos;
        *found = true;
        return true;
      }
      h_gear = (h_gear << 1) + gear[data[pos]] - gear[data[pos - w]];
      h_norm = (h_norm << 1) + norm_gear[data[pos]] - norm_gear[data[pos - w]];
    }
  }
  for (; i < cut_limit; ++i) {
    if (probes) ++*probes;
    if ((h_gear & mask_lo) == 0 && ((h_norm >> norm_shift) & mask_hi) == 0) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h_gear = (h_gear << 1) + gear[data[i]] - gear[data[i - w]];
    h_norm = (h_norm << 1) + norm_gear[data[i]] - norm_gear[data[i - w]];
  }
  if (h_gear_out) *h_gear_out = h_gear;
  if (h_norm_out) *h_norm_out = h_norm;
  return false;
}

}  // namespace

uint32_t MixSeedHash(uint32_t h, uint32_t seed, size_t pos) {
  if (seed == 0) return h;
  const uint32_t mixed = seed ^ static_cast<uint32_t>(pos >> 6);
  return h ^ Rotl32(mixed, (seed & 15u) + 1u);
}

uint32_t SelectPhaseMask(size_t d, uint32_t avg_size, uint8_t nc_level) {
  if (nc_level == 0) return BuildMask(avg_size);
  const size_t phase_lo = static_cast<size_t>(avg_size) >> nc_level;
  const size_t phase_hi = static_cast<size_t>(avg_size) << nc_level;
  if (d < phase_lo) return BuildMask(avg_size << 1);
  if (d < phase_hi) return BuildMask(avg_size);
  return BuildMask(avg_size >> 1);
}

void Build2FMasks(uint32_t avg_size, uint8_t nc_level, uint32_t* mask_lo,
                  uint32_t* mask_hi, uint32_t* norm_shift) {
  uint32_t total_bits = 0;
  uint32_t v = avg_size;
  while (v > 1) {
    v >>= 1;
    ++total_bits;
  }
  if (total_bits == 0) total_bits = 1;
  if (total_bits > 31) total_bits = 31;

  uint32_t hi_bits = total_bits / 2;
  if (nc_level > 0) {
    const uint32_t nc_cap =
        total_bits / (static_cast<uint32_t>(nc_level) + 1u);
    if (nc_cap > 0 && nc_cap < hi_bits) hi_bits = nc_cap;
  }
  if (hi_bits == 0) hi_bits = 1;
  uint32_t lo_bits = total_bits - hi_bits;
  if (lo_bits == 0) {
    lo_bits = 1;
    if (hi_bits > 1) --hi_bits;
  }

  *mask_lo = (1u << lo_bits) - 1u;
  *mask_hi = (1u << hi_bits) - 1u;
  *norm_shift = lo_bits;
}

namespace {

bool ScanGearCutAnScalar(const uint8_t* data, size_t scan_start,
                         size_t cut_limit, size_t chunk_start, size_t pos_bias,
                         uint32_t w, uint32_t avg_size, uint8_t nc_level,
                         uint32_t seed, const uint32_t gear[256],
                         size_t* out_cut, bool* found, uint32_t h_resume,
                         bool use_h_resume, uint32_t* h_out, uint64_t* probes) {
  uint32_t h =
      use_h_resume ? h_resume : InitWindowHash(data, scan_start, w, gear);
  size_t i = scan_start;
  for (; i + 8 <= cut_limit; i += 8) {
    for (size_t j = 0; j < 8; ++j) {
      const size_t pos = i + j;
      if (probes) ++*probes;
      const size_t abs_pos = pos_bias + pos;
      const size_t d = abs_pos - chunk_start;
      const uint32_t mask = SelectPhaseMask(d, avg_size, nc_level);
      const uint32_t h_test = MixSeedHash(h, seed, abs_pos);
      if ((h_test & mask) == 0) {
        *out_cut = pos;
        *found = true;
        return true;
      }
      h = (h << 1) + gear[data[pos]] - gear[data[pos - w]];
    }
  }
  for (; i < cut_limit; ++i) {
    if (probes) ++*probes;
    const size_t abs_pos = pos_bias + i;
    const size_t d = abs_pos - chunk_start;
    const uint32_t mask = SelectPhaseMask(d, avg_size, nc_level);
    const uint32_t h_test = MixSeedHash(h, seed, abs_pos);
    if ((h_test & mask) == 0) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h = (h << 1) + gear[data[i]] - gear[data[i - w]];
  }
  if (h_out) *h_out = h;
  return false;
}

}  // namespace

uint32_t BuildMask(uint32_t avg_size) {
  uint32_t bits = 0;
  uint32_t v = avg_size;
  while (v > 1) {
    v >>= 1;
    ++bits;
  }
  if (bits == 0) bits = 1;
  if (bits > 31) bits = 31;
  return (1u << bits) - 1u;
}

uint32_t BuildNormMask(uint32_t avg_size, uint8_t nc_level) {
  if (nc_level == 0) return 0;
  uint32_t shifted = avg_size >> nc_level;
  if (shifted == 0) shifted = 1;
  return BuildMask(shifted);
}

void InitGearTable(uint32_t gear[256]) {
  for (int i = 0; i < 256; ++i) {
    uint32_t h = static_cast<uint32_t>(i);
    for (int j = 0; j < 16; ++j) {
      h = (h >> 1) ^
          (static_cast<uint32_t>(-static_cast<int32_t>(h & 1u)) & 0xD0000001u);
    }
    gear[i] = h;
  }
}

void InitKeyedGearTable(uint32_t gear[256], uint32_t seed) {
  InitGearTable(gear);
  if (seed == 0) return;
  uint64_t state = seed;
  for (int i = 0; i < 256; ++i) {
    state = SplitMix64(state);
    gear[i] ^= static_cast<uint32_t>(state);
    gear[i] = Rotl32(gear[i], (seed + static_cast<uint32_t>(i)) & 15u);
  }
}

bool ScanGearCut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                 uint32_t w, uint32_t mask, const uint32_t gear[256],
                 size_t* out_cut, bool* found, uint64_t* probes) {
  if (!data || !out_cut || !found) return false;
  *found = false;

#if defined(__AVX2__) || defined(_M_AVX2)
  uint32_t h = InitWindowHash(data, scan_start, w, gear);
  size_t i = scan_start;
  for (; i + 8 <= cut_limit; i += 8) {
    _mm_prefetch(reinterpret_cast<const char*>(data + i + 64), _MM_HINT_T0);
    for (size_t j = 0; j < 8 && i + j < cut_limit; ++j) {
      const size_t pos = i + j;
      if (probes) ++*probes;
      if ((h & mask) == 0) {
        *out_cut = pos;
        *found = true;
        return true;
      }
      h = (h << 1) + gear[data[pos]] - gear[data[pos - w]];
    }
  }
  for (; i < cut_limit; ++i) {
    if (probes) ++*probes;
    if ((h & mask) == 0) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h = (h << 1) + gear[data[i]] - gear[data[i - w]];
  }
  return false;
#else
  return ScanGearCutScalar(data, scan_start, cut_limit, w, mask, gear, out_cut,
                           found, probes);
#endif
}

bool ScanGearCutNc(const uint8_t* data, size_t scan_start, size_t cut_limit,
                   uint32_t w, uint32_t mask, uint32_t norm_mask,
                   const uint32_t gear[256], size_t* out_cut, bool* found,
                   uint64_t* probes) {
  if (!data || !out_cut || !found) return false;
  *found = false;
  if (norm_mask == 0) {
    return ScanGearCut(data, scan_start, cut_limit, w, mask, gear, out_cut,
                       found, probes);
  }

#if defined(__AVX2__) || defined(_M_AVX2)
  uint32_t h = InitWindowHash(data, scan_start, w, gear);
  size_t i = scan_start;
  for (; i + 8 <= cut_limit; i += 8) {
    _mm_prefetch(reinterpret_cast<const char*>(data + i + 64), _MM_HINT_T0);
    for (size_t j = 0; j < 8 && i + j < cut_limit; ++j) {
      const size_t pos = i + j;
      if (probes) ++*probes;
      if ((h & mask) == 0 && (h & norm_mask) == norm_mask) {
        *out_cut = pos;
        *found = true;
        return true;
      }
      h = (h << 1) + gear[data[pos]] - gear[data[pos - w]];
    }
  }
  for (; i < cut_limit; ++i) {
    if (probes) ++*probes;
    if ((h & mask) == 0 && (h & norm_mask) == norm_mask) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h = (h << 1) + gear[data[i]] - gear[data[i - w]];
  }
  return false;
#else
  return ScanGearCutNcScalar(data, scan_start, cut_limit, w, mask, norm_mask,
                             gear, out_cut, found, probes);
#endif
}

bool ScanGearCut2F(const uint8_t* data, size_t scan_start, size_t cut_limit,
                   uint32_t w, uint32_t mask_lo, uint32_t mask_hi,
                   uint32_t norm_shift, const uint32_t gear[256],
                   const uint32_t norm_gear[256], size_t* out_cut, bool* found,
                   uint32_t h_gear_resume, uint32_t h_norm_resume,
                   bool use_h_resume, uint32_t* h_gear_out, uint32_t* h_norm_out,
                   uint64_t* probes) {
  if (!data || !out_cut || !found) return false;
  *found = false;

#if defined(__AVX2__) || defined(_M_AVX2)
  uint32_t h_gear =
      use_h_resume ? h_gear_resume : InitWindowHash(data, scan_start, w, gear);
  uint32_t h_norm = use_h_resume
                        ? h_norm_resume
                        : InitWindowHash(data, scan_start, w, norm_gear);
  size_t i = scan_start;
  for (; i + 8 <= cut_limit; i += 8) {
    _mm_prefetch(reinterpret_cast<const char*>(data + i + 64), _MM_HINT_T0);
    for (size_t j = 0; j < 8 && i + j < cut_limit; ++j) {
      const size_t pos = i + j;
      if (probes) ++*probes;
      if ((h_gear & mask_lo) == 0 &&
          ((h_norm >> norm_shift) & mask_hi) == 0) {
        *out_cut = pos;
        *found = true;
        return true;
      }
      h_gear = (h_gear << 1) + gear[data[pos]] - gear[data[pos - w]];
      h_norm = (h_norm << 1) + norm_gear[data[pos]] - norm_gear[data[pos - w]];
    }
  }
  for (; i < cut_limit; ++i) {
    if (probes) ++*probes;
    if ((h_gear & mask_lo) == 0 && ((h_norm >> norm_shift) & mask_hi) == 0) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h_gear = (h_gear << 1) + gear[data[i]] - gear[data[i - w]];
    h_norm = (h_norm << 1) + norm_gear[data[i]] - norm_gear[data[i - w]];
  }
  if (h_gear_out) *h_gear_out = h_gear;
  if (h_norm_out) *h_norm_out = h_norm;
  return false;
#else
  return ScanGearCut2FScalar(data, scan_start, cut_limit, w, mask_lo, mask_hi,
                             norm_shift, gear, norm_gear, out_cut, found,
                             h_gear_resume, h_norm_resume, use_h_resume,
                             h_gear_out, h_norm_out, probes);
#endif
}

bool ScanGearCutAn(const uint8_t* data, size_t scan_start, size_t cut_limit,
                   size_t chunk_start, size_t pos_bias, uint32_t w,
                   uint32_t avg_size, uint8_t nc_level, uint32_t seed,
                   const uint32_t gear[256], size_t* out_cut, bool* found,
                   uint32_t h_resume, bool use_h_resume, uint32_t* h_out,
                   uint64_t* probes) {
  if (!data || !out_cut || !found) return false;
  *found = false;

#if defined(__AVX2__) || defined(_M_AVX2)
  uint32_t h =
      use_h_resume ? h_resume : InitWindowHash(data, scan_start, w, gear);
  size_t i = scan_start;
  for (; i + 8 <= cut_limit; i += 8) {
    _mm_prefetch(reinterpret_cast<const char*>(data + i + 64), _MM_HINT_T0);
    for (size_t j = 0; j < 8 && i + j < cut_limit; ++j) {
      const size_t pos = i + j;
      if (probes) ++*probes;
      const size_t abs_pos = pos_bias + pos;
      const size_t d = abs_pos - chunk_start;
      const uint32_t mask = SelectPhaseMask(d, avg_size, nc_level);
      const uint32_t h_test = MixSeedHash(h, seed, abs_pos);
      if ((h_test & mask) == 0) {
        *out_cut = pos;
        *found = true;
        return true;
      }
      h = (h << 1) + gear[data[pos]] - gear[data[pos - w]];
    }
  }
  for (; i < cut_limit; ++i) {
    if (probes) ++*probes;
    const size_t abs_pos = pos_bias + i;
    const size_t d = abs_pos - chunk_start;
    const uint32_t mask = SelectPhaseMask(d, avg_size, nc_level);
    const uint32_t h_test = MixSeedHash(h, seed, abs_pos);
    if ((h_test & mask) == 0) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h = (h << 1) + gear[data[i]] - gear[data[i - w]];
  }
  if (h_out) *h_out = h;
  return false;
#else
  return ScanGearCutAnScalar(data, scan_start, cut_limit, chunk_start, pos_bias,
                             w, avg_size, nc_level, seed, gear, out_cut,
                             found, h_resume, use_h_resume, h_out, probes);
#endif
}

}  // namespace gtcdc_internal
}  // namespace ebbackup
