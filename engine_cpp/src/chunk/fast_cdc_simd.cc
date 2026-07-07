#include "ebbackup/chunk/fast_cdc_internal.h"

#if defined(__AVX2__) || defined(_M_AVX2)
#include <immintrin.h>
#endif

namespace ebbackup {
namespace fastcdc_internal {

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

uint32_t InitWindowHash(const uint8_t* data, size_t end, uint32_t w,
                        const uint32_t gear[256]) {
  uint32_t h = 0;
  const size_t start = end - w;
  for (size_t i = start; i < end; ++i) {
    h = (h << 1) + gear[data[i]];
  }
  return h;
}

namespace {

bool ScanGearCutScalar(const uint8_t* data, size_t scan_start, size_t cut_limit,
                       uint32_t w, uint32_t mask, const uint32_t gear[256],
                       size_t* out_cut, bool* found) {
  uint32_t h = InitWindowHash(data, scan_start, w, gear);
  size_t i = scan_start;
  for (; i + 8 <= cut_limit; i += 8) {
    for (size_t j = 0; j < 8; ++j) {
      const size_t pos = i + j;
      if ((h & mask) == 0) {
        *out_cut = pos;
        *found = true;
        return true;
      }
      h = (h << 1) + gear[data[pos]] - gear[data[pos - w]];
    }
  }
  for (; i < cut_limit; ++i) {
    if ((h & mask) == 0) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h = (h << 1) + gear[data[i]] - gear[data[i - w]];
  }
  return false;
}

}  // namespace

bool ScanGearCut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                 uint32_t w, uint32_t mask, const uint32_t gear[256],
                 size_t* out_cut, bool* found) {
  if (!data || !out_cut || !found) return false;
  *found = false;

#if defined(__AVX2__) || defined(_M_AVX2)
  uint32_t h = InitWindowHash(data, scan_start, w, gear);
  size_t i = scan_start;
  for (; i + 8 <= cut_limit; i += 8) {
    _mm_prefetch(reinterpret_cast<const char*>(data + i + 64), _MM_HINT_T0);
    for (size_t j = 0; j < 8 && i + j < cut_limit; ++j) {
      const size_t pos = i + j;
      if ((h & mask) == 0) {
        *out_cut = pos;
        *found = true;
        return true;
      }
      h = (h << 1) + gear[data[pos]] - gear[data[pos - w]];
    }
  }
  for (; i < cut_limit; ++i) {
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
                           found);
#endif
}

}  // namespace fastcdc_internal
}  // namespace ebbackup
