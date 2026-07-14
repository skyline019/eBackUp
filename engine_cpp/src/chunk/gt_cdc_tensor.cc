#include "ebbackup/chunk/gt_cdc_internal.h"

#include <algorithm>

#if defined(__AVX2__) || defined(_M_AVX2)
#include <immintrin.h>
#endif

namespace ebbackup {
namespace gtcdc_internal {

namespace {

bool VectorFirstHit8(const uint8_t* data, size_t i, size_t cut_limit, uint32_t h0,
                     uint32_t alpha, const uint32_t beta_table[256],
                     const uint32_t pow[kAlphaPowTableSize], uint32_t mask,
                     size_t* out_cut, uint32_t* h_out, GtCdcScanStats* stats) {
  const size_t n = std::min<size_t>(8, cut_limit - i);
  if (n == 0) return false;

  uint32_t partial = 0;
  uint32_t h_check[8]{};

#if defined(__AVX2__) || defined(_M_AVX2)
  if (n == 8) {
    for (size_t k = 0; k < 8; ++k) {
      h_check[k] = pow[k] * h0 + partial;
      partial = alpha * partial + beta_table[data[i + k]];
    }
    __m256i hv = _mm256_set_epi32(
        static_cast<int>(h_check[7] & mask), static_cast<int>(h_check[6] & mask),
        static_cast<int>(h_check[5] & mask), static_cast<int>(h_check[4] & mask),
        static_cast<int>(h_check[3] & mask), static_cast<int>(h_check[2] & mask),
        static_cast<int>(h_check[1] & mask), static_cast<int>(h_check[0] & mask));
    const __m256i zero = _mm256_setzero_si256();
    const __m256i eq = _mm256_cmpeq_epi32(hv, zero);
    const int hit_mask = _mm256_movemask_ps(_mm256_castsi256_ps(eq));
    if (hit_mask != 0) {
      const unsigned long first =
          _tzcnt_u32(static_cast<uint32_t>(hit_mask));
      *out_cut = i + first;
      if (h_out) *h_out = h_check[first];
      if (stats) ++stats->vector8_groups;
      return true;
    }
    if (h_out) *h_out = pow[8] * h0 + partial;
    if (stats) ++stats->vector8_groups;
    return false;
  }
#endif

  for (size_t k = 0; k < n; ++k) {
    h_check[k] = pow[k] * h0 + partial;
    if ((h_check[k] & mask) == 0) {
      *out_cut = i + k;
      if (h_out) *h_out = h_check[k];
      if (stats) ++stats->vector8_groups;
      return true;
    }
    partial = alpha * partial + beta_table[data[i + k]];
  }
  if (h_out) *h_out = pow[n] * h0 + partial;
  if (stats && n > 0) ++stats->vector8_groups;
  return false;
}

bool ScanBlockLeapfrogV2(const uint8_t* data, size_t scan_start, size_t cut_limit,
                         uint32_t h_initial, uint32_t alpha,
                         const uint32_t beta_table[256], uint32_t mask,
                         uint32_t block_B, const uint32_t pow[kAlphaPowTableSize],
                         size_t* out_cut, bool* found, uint32_t* h_out,
                         GtCdcScanStats* stats) {
  uint32_t h = h_initial;
  size_t pos = scan_start;
  if (block_B == 0) block_B = 64;

  while (pos < cut_limit) {
    const size_t block_end = std::min(pos + block_B, cut_limit);

#if defined(__AVX2__) || defined(_M_AVX2)
    if (block_end < cut_limit) {
      _mm_prefetch(reinterpret_cast<const char*>(data + block_end), _MM_HINT_T0);
    }
#endif

    size_t i = pos;
    while (i < block_end) {
      size_t cut = i;
      uint32_t h_after = h;
      if (VectorFirstHit8(data, i, block_end, h, alpha, beta_table, pow, mask,
                          &cut, &h_after, stats)) {
        *out_cut = cut;
        *found = true;
        if (h_out) *h_out = h_after;
        return true;
      }
      h = h_after;
      i += std::min<size_t>(8, block_end - i);
    }

    if (stats && block_end > pos) {
      ++stats->blocks_composed;
    }
    pos = block_end;
  }

  if (h_out) *h_out = h;
  return false;
}

}  // namespace

uint32_t BlockFingerprint64(const uint8_t* data, uint32_t alpha,
                            const uint32_t beta_table[256]) {
#if defined(__AVX2__) || defined(_M_AVX2)
  uint32_t fp = 0;
  for (size_t i = 0; i < 64; i += 8) {
    const __m128i bytes8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(data + i));
    const __m256i vidx = _mm256_cvtepu8_epi32(bytes8);
    const __m256i vbeta =
        _mm256_i32gather_epi32(reinterpret_cast<const int*>(beta_table), vidx, 4);
    alignas(32) uint32_t betas[8];
    _mm256_store_si256(reinterpret_cast<__m256i*>(betas), vbeta);
    for (size_t k = 0; k < 8; ++k) {
      fp = alpha * fp + betas[k];
    }
  }
  return fp;
#else
  return BlockFingerprint(data, 64, alpha, beta_table);
#endif
}

uint32_t BlockFingerprintFast(const uint8_t* data, size_t len, uint32_t alpha,
                              const uint32_t beta_table[256]) {
  if (len == 64) {
    return BlockFingerprint64(data, alpha, beta_table);
  }
  return BlockFingerprint(data, len, alpha, beta_table);
}

bool GtCdcScan(const uint8_t* data, size_t scan_start, size_t cut_limit,
               uint32_t h_initial, uint32_t alpha,
               const uint32_t beta_table[256], uint32_t mask, uint32_t block_B,
               const uint32_t pow[kAlphaPowTableSize], size_t* out_cut,
               bool* found, uint32_t* h_out, GtCdcScanStats* stats) {
  if (!data || !out_cut || !found || !pow) return false;
  *found = false;

#if !defined(__AVX2__) && !defined(_M_AVX2)
  (void)block_B;
  (void)stats;
  (void)h_initial;
  (void)alpha;
  (void)beta_table;
  (void)mask;
  (void)cut_limit;
  (void)scan_start;
  (void)h_out;
  return false;
#else
  return ScanBlockLeapfrogV2(data, scan_start, cut_limit, h_initial, alpha,
                             beta_table, mask, block_B, pow, out_cut, found,
                             h_out, stats);
#endif
}

}  // namespace gtcdc_internal
}  // namespace ebbackup
