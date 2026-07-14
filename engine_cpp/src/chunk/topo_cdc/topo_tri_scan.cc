#include "ebbackup/chunk/topo_tri_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ebbackup/chunk/topo_cdc_internal.h"

#if defined(__AVX2__) || defined(_M_AVX2)
#include <immintrin.h>
#endif

namespace ebbackup {
namespace topo_tri_internal {

namespace {

inline float Cross(float ax, float ay, float bx, float by) {
  return ax * by - ay * bx;
}

void PushRecent(uint32_t* recent_h, uint32_t* recent_t, uint32_t* count,
                uint8_t k_points, uint32_t h, uint32_t t) {
  if (*count < k_points) {
    recent_h[*count] = h;
    recent_t[*count] = t;
    ++*count;
    return;
  }
  for (uint32_t i = 1; i < k_points; ++i) {
    recent_h[i - 1] = recent_h[i];
    recent_t[i - 1] = recent_t[i];
  }
  recent_h[k_points - 1] = h;
  recent_t[k_points - 1] = t;
}

void ReplayTriState(const uint8_t* data, size_t scan_start, size_t pos,
                    uint32_t w, const uint32_t gear[256], uint8_t k_points,
                    uint32_t* h, uint32_t* recent_h, uint32_t* recent_t,
                    uint32_t* recent_count) {
  *h = topo_cdc_internal::InitWindowHash(data, scan_start, w, gear);
  *recent_count = 0;
  for (size_t i = scan_start; i < pos; ++i) {
    PushRecent(recent_h, recent_t, recent_count, k_points, *h,
               static_cast<uint32_t>(i));
    *h = ((*h << 1) + gear[data[i]] - gear[data[i - w]]) & 0xFFFFFFFFu;
  }
}

inline bool ProbeTriAt(uint32_t* h, uint32_t mask, uint32_t w,
                       const uint32_t gear[256], uint8_t k_points,
                       uint32_t q_mod, uint32_t* recent_h, uint32_t* recent_t,
                       uint32_t* recent_count, const uint8_t* data, size_t pos,
                       size_t* out_cut) {
  const bool primary_ok = ((*h & mask) == 0);
  PushRecent(recent_h, recent_t, recent_count, k_points, *h,
             static_cast<uint32_t>(pos));
  if (primary_ok &&
      DelaunayFlipCount(recent_h, recent_t, *recent_count, k_points, q_mod) >=
          1) {
    *out_cut = pos;
    return true;
  }
  *h = ((*h << 1) + gear[data[pos]] - gear[data[pos - w]]) & 0xFFFFFFFFu;
  return false;
}

bool RunTriScanLoop(const uint8_t* data, size_t p_start, size_t cut_limit,
                    uint32_t w, uint32_t mask, const uint32_t gear[256],
                    uint8_t k_points, uint32_t q_mod, uint32_t* h,
                    uint32_t* recent_h, uint32_t* recent_t,
                    uint32_t* recent_count, size_t* out_cut,
                    uint64_t* probes) {
  size_t p = p_start;
#if defined(__AVX2__) || defined(_M_AVX2)
  for (; p + 8 <= cut_limit; p += 8) {
    _mm_prefetch(reinterpret_cast<const char*>(data + p + 64), _MM_HINT_T0);
    for (size_t j = 0; j < 8; ++j) {
      const size_t pos = p + j;
      if (probes) ++*probes;
      if (ProbeTriAt(h, mask, w, gear, k_points, q_mod, recent_h, recent_t,
                     recent_count, data, pos, out_cut)) {
        return true;
      }
    }
  }
#endif
  for (; p < cut_limit; ++p) {
    if (probes) ++*probes;
    if (ProbeTriAt(h, mask, w, gear, k_points, q_mod, recent_h, recent_t,
                   recent_count, data, p, out_cut)) {
      return true;
    }
  }
  return false;
}

}  // namespace

uint32_t DelaunayFlipCount(const uint32_t* recent_h, const uint32_t* recent_t,
                           uint32_t count, uint8_t k_points, uint32_t q_mod) {
  if (!recent_h || !recent_t || count < 4 || k_points == 0) return 0;

  float pts_x[kTriMaxKPoints]{};
  float pts_y[kTriMaxKPoints]{};
  const uint32_t k = std::min(count, static_cast<uint32_t>(k_points));
  for (uint32_t i = 0; i < k; ++i) {
    pts_x[i] = static_cast<float>(recent_t[i] % k_points);
    pts_y[i] = static_cast<float>(recent_h[i] % q_mod);
  }

  uint32_t flips = 0;
  for (uint32_t i = 1; i + 1 < k; ++i) {
    const float ax = pts_x[0];
    const float ay = pts_y[0];
    const float bx = pts_x[i];
    const float by = pts_y[i];
    const float cx = pts_x[i + 1];
    const float cy = pts_y[i + 1];
    if (Cross(bx - ax, by - ay, cx - ax, cy - ay) <= 0.0f) ++flips;
  }
  for (uint32_t i = 0; i + 2 < k; ++i) {
    const float ax = pts_x[i];
    const float ay = pts_y[i];
    const float bx = pts_x[i + 1];
    const float by = pts_y[i + 1];
    const float cx = pts_x[i + 2];
    const float cy = pts_y[i + 2];
    if (Cross(bx - ax, by - ay, cx - ax, cy - ay) <= 0.0f) ++flips;
  }
  return flips;
}

bool ScanTriCut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                const TopoCdcConfig& cfg, uint32_t mask,
                const uint32_t gear[256], size_t* out_cut, bool* found,
                uint64_t* probes) {
  if (!out_cut || !found) return false;
  *found = false;
  if (scan_start >= cut_limit || scan_start < cfg.window_w) return false;

  const uint32_t w = cfg.window_w;
  const uint8_t k_points = std::max<uint8_t>(cfg.tri_k_points, 4);
  uint32_t recent_h[kTriMaxKPoints]{};
  uint32_t recent_t[kTriMaxKPoints]{};
  uint32_t recent_count = 0;
  uint32_t h = 0;
  ReplayTriState(data, scan_start, scan_start, w, gear, k_points, &h, recent_h,
                 recent_t, &recent_count);

  size_t cut = scan_start;
  if (RunTriScanLoop(data, scan_start, cut_limit, w, mask, gear, k_points,
                     cfg.tri_q_mod, &h, recent_h, recent_t, &recent_count, &cut,
                     probes)) {
    *out_cut = cut;
    *found = true;
    return true;
  }
  return false;
}

void ClearTopoTriResume(TopoTriResume* resume) {
  if (!resume) return;
  resume->h = 0;
  resume->scan_rel = 0;
  resume->in_scan = false;
  resume->window_w = 64;
  resume->recent_count = 0;
}

bool ProcessTriChunk(const uint8_t* data, size_t len, size_t chunk_pos,
                     bool allow_tail_cut, const TopoCdcConfig& cfg,
                     uint32_t mask, const uint32_t gear[256],
                     TopoTriResume* resume, size_t* out_cut, bool* chunk_done,
                     uint64_t* probes) {
  if (!resume || !out_cut || !chunk_done) return false;
  *chunk_done = false;

  const uint32_t w = cfg.window_w;
  const uint8_t k_points = std::max<uint8_t>(cfg.tri_k_points, 4);
  const size_t scan_start = chunk_pos + cfg.min_size;
  const size_t cut_limit = std::min(chunk_pos + cfg.max_size, len);
  if (scan_start >= cut_limit) {
    if (allow_tail_cut && cut_limit > chunk_pos) {
      *out_cut = cut_limit;
      *chunk_done = true;
      ClearTopoTriResume(resume);
    }
    return true;
  }
  if (scan_start < w) {
    if (allow_tail_cut) {
      *out_cut = cut_limit;
      *chunk_done = true;
      ClearTopoTriResume(resume);
    }
    return true;
  }

  size_t p = scan_start;
  if (resume->in_scan && resume->scan_rel >= scan_start &&
      resume->scan_rel < cut_limit) {
    p = resume->scan_rel;
  } else {
    resume->window_w = w;
    resume->in_scan = true;
    p = scan_start;
  }

  ReplayTriState(data, scan_start, p, w, gear, k_points, &resume->h,
                 resume->recent_h, resume->recent_t, &resume->recent_count);

  size_t cut = p;
  if (RunTriScanLoop(data, p, cut_limit, w, mask, gear, k_points, cfg.tri_q_mod,
                     &resume->h, resume->recent_h, resume->recent_t,
                     &resume->recent_count, &cut, probes)) {
    *out_cut = cut;
    *chunk_done = true;
    ClearTopoTriResume(resume);
    return true;
  }

  resume->scan_rel = cut_limit;
  resume->in_scan = true;

  const size_t remaining = len - chunk_pos;
  if (remaining > cfg.max_size) {
    *out_cut = cut_limit;
    *chunk_done = true;
    ClearTopoTriResume(resume);
    return true;
  }

  if (allow_tail_cut && len > chunk_pos) {
    *out_cut = len;
    *chunk_done = true;
    ClearTopoTriResume(resume);
    return true;
  }
  return true;
}

}  // namespace topo_tri_internal
}  // namespace ebbackup
