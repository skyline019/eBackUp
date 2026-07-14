#include "ebbackup/chunk/topo_ph_internal.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "ebbackup/chunk/gt_cdc_internal.h"
#include "ebbackup/common/status.h"

namespace ebbackup {
namespace topo_ph_internal {

namespace {

// Fixed R^2 grid in quantized plane (q_mod scale).
constexpr uint32_t kEpsR2[kPhH0EpsCount] = {1u, 4u, 16u, 64u, 256u};

inline int64_t CrossI(int64_t ax, int64_t ay, int64_t bx, int64_t by) {
  return ax * by - ay * bx;
}

void PushLandmark(uint32_t* recent_h, uint32_t* recent_t, uint32_t* count,
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

uint32_t VrBeta0(const uint32_t* recent_h, const uint32_t* recent_t, uint32_t k,
                 uint32_t q_mod, uint32_t r2) {
  if (k == 0) return 0;
  uint8_t parent[kTopoPhMaxK];
  for (uint32_t i = 0; i < k; ++i) parent[i] = static_cast<uint8_t>(i);
  auto find = [&](uint8_t x) {
    while (parent[x] != x) x = parent[x];
    return x;
  };
  auto unite = [&](uint8_t a, uint8_t b) {
    a = find(a);
    b = find(b);
    if (a != b) parent[a] = b;
  };

  for (uint32_t i = 0; i < k; ++i) {
    const int64_t xi = static_cast<int64_t>(recent_t[i] % q_mod);
    const int64_t yi = static_cast<int64_t>(recent_h[i] % q_mod);
    for (uint32_t j = i + 1; j < k; ++j) {
      const int64_t xj = static_cast<int64_t>(recent_t[j] % q_mod);
      const int64_t yj = static_cast<int64_t>(recent_h[j] % q_mod);
      const int64_t dx = xi - xj;
      const int64_t dy = yi - yj;
      const uint64_t d2 = static_cast<uint64_t>(dx * dx + dy * dy);
      if (d2 <= r2) unite(static_cast<uint8_t>(i), static_cast<uint8_t>(j));
    }
  }
  uint32_t comps = 0;
  for (uint32_t i = 0; i < k; ++i) {
    if (find(static_cast<uint8_t>(i)) == i) ++comps;
  }
  return comps;
}

void ReplayTo(const uint8_t* data, size_t scan_start, size_t pos, uint32_t w,
              const uint32_t gear[256], uint32_t* h) {
  *h = InitWindowHash(data, scan_start, w, gear);
  for (size_t i = scan_start; i < pos; ++i) {
    *h = ((*h << 1) + gear[data[i]] - gear[data[i - w]]) & 0xFFFFFFFFu;
  }
}

bool ProbeAt(uint32_t* h, uint32_t mask, uint32_t w, const uint32_t gear[256],
             const TopoPhConfig& cfg, TopoPhResume* st, const uint8_t* data,
             size_t pos, size_t* out_cut) {
  const uint8_t k_points = ClampKPoints(cfg.k_points);
  const bool primary_ok = ((*h & mask) == 0);
  if (primary_ok) {
    if (cfg.kernel == TopoPhKernel::kTriV2) {
      const uint32_t before = FlipCountFixed(
          st->recent_h, st->recent_t, st->recent_count, k_points, cfg.q_mod);
      PushLandmark(st->recent_h, st->recent_t, &st->recent_count, k_points, *h,
                   static_cast<uint32_t>(pos));
      const uint32_t after = FlipCountFixed(
          st->recent_h, st->recent_t, st->recent_count, k_points, cfg.q_mod);
      st->prev_flip = after;
      st->has_prev = true;
      if (after != before) {
        *out_cut = pos;
        *h = ((*h << 1) + gear[data[pos]] - gear[data[pos - w]]) & 0xFFFFFFFFu;
        return true;
      }
    } else {
      uint32_t cur[kPhH0EpsCount]{};
      PushLandmark(st->recent_h, st->recent_t, &st->recent_count, k_points, *h,
                   static_cast<uint32_t>(pos));
      PhH0BettiVec(st->recent_h, st->recent_t, st->recent_count, k_points,
                   cfg.q_mod, cur);
      bool ph_ok = false;
      if (st->has_prev) {
        ph_ok = PhH0Delta(st->prev_beta0, cur);
      }
      std::memcpy(st->prev_beta0, cur, sizeof(cur));
      st->has_prev = true;
      if (ph_ok) {
        *out_cut = pos;
        *h = ((*h << 1) + gear[data[pos]] - gear[data[pos - w]]) & 0xFFFFFFFFu;
        return true;
      }
    }
  }
  *h = ((*h << 1) + gear[data[pos]] - gear[data[pos - w]]) & 0xFFFFFFFFu;
  return false;
}

}  // namespace

void InitGearTable(uint32_t gear[256], uint32_t seed) {
  gtcdc_internal::InitKeyedGearTable(gear, seed);
}

void BuildPhMasks(uint32_t avg_size, uint16_t calib_permille, uint8_t topo_shift,
                  uint32_t* mask_out) {
  if (!mask_out) return;
  if (calib_permille > 0) {
    const double p =
        std::max(static_cast<double>(calib_permille) / 1000.0, 0.001);
    const uint32_t effective = std::max<uint32_t>(
        static_cast<uint32_t>(static_cast<double>(avg_size) * p), 1u);
    *mask_out = gtcdc_internal::BuildMask(effective);
    return;
  }
  const uint32_t shifted = std::max<uint32_t>(avg_size >> topo_shift, 1u);
  *mask_out = gtcdc_internal::BuildMask(shifted);
}

uint32_t InitWindowHash(const uint8_t* data, size_t end, uint32_t w,
                        const uint32_t gear[256]) {
  uint32_t h = 0;
  for (size_t i = end - w; i < end; ++i) h = (h << 1) + gear[data[i]];
  return h;
}

void FillTopoPhCalibSample(uint8_t* out, size_t len, uint32_t seed) {
  if (!out || len == 0) return;
  uint32_t s = seed ? seed : 0xA5A5A5A5u;
  for (size_t i = 0; i < len; ++i) {
    s = s * 1664525u + 1013904223u;
    out[i] = static_cast<uint8_t>((s >> 16) & 0xFFu);
  }
}

uint32_t FlipCountFixed(const uint32_t* recent_h, const uint32_t* recent_t,
                        uint32_t count, uint8_t k_points, uint32_t q_mod) {
  if (!recent_h || !recent_t || count < 4 || k_points == 0) return 0;
  const uint32_t k = std::min(count, static_cast<uint32_t>(k_points));
  uint32_t flips = 0;
  for (uint32_t i = 1; i + 1 < k; ++i) {
    const int64_t ax = static_cast<int64_t>(recent_t[0] % k_points);
    const int64_t ay = static_cast<int64_t>(recent_h[0] % q_mod);
    const int64_t bx = static_cast<int64_t>(recent_t[i] % k_points);
    const int64_t by = static_cast<int64_t>(recent_h[i] % q_mod);
    const int64_t cx = static_cast<int64_t>(recent_t[i + 1] % k_points);
    const int64_t cy = static_cast<int64_t>(recent_h[i + 1] % q_mod);
    if (CrossI(bx - ax, by - ay, cx - ax, cy - ay) <= 0) ++flips;
  }
  for (uint32_t i = 0; i + 2 < k; ++i) {
    const int64_t ax = static_cast<int64_t>(recent_t[i] % k_points);
    const int64_t ay = static_cast<int64_t>(recent_h[i] % q_mod);
    const int64_t bx = static_cast<int64_t>(recent_t[i + 1] % k_points);
    const int64_t by = static_cast<int64_t>(recent_h[i + 1] % q_mod);
    const int64_t cx = static_cast<int64_t>(recent_t[i + 2] % k_points);
    const int64_t cy = static_cast<int64_t>(recent_h[i + 2] % q_mod);
    if (CrossI(bx - ax, by - ay, cx - ax, cy - ay) <= 0) ++flips;
  }
  return flips;
}

void PhH0BettiVec(const uint32_t* recent_h, const uint32_t* recent_t,
                  uint32_t count, uint8_t k_points, uint32_t q_mod,
                  uint32_t out_beta0[kPhH0EpsCount]) {
  const uint32_t k =
      std::min(count, static_cast<uint32_t>(ClampKPoints(k_points)));
  for (size_t i = 0; i < kPhH0EpsCount; ++i) {
    out_beta0[i] = VrBeta0(recent_h, recent_t, k, q_mod, kEpsR2[i]);
  }
}

bool PhH0Delta(const uint32_t* prev, const uint32_t* cur) {
  if (!prev || !cur) return false;
  for (size_t i = 0; i < kPhH0EpsCount; ++i) {
    if (prev[i] != cur[i]) return true;
  }
  return false;
}

uint16_t CalibrateTopoPhPermille(const uint8_t* sample, size_t sample_len,
                                 const TopoPhConfig& cfg, uint32_t seed) {
  if (!sample || sample_len < cfg.min_size + cfg.window_w + 1024) return 500;

  uint32_t gear[256]{};
  InitGearTable(gear, seed);
  uint32_t mask = 0;
  BuildPhMasks(cfg.avg_size, 0, cfg.topo_shift, &mask);

  const uint8_t k_points = ClampKPoints(cfg.k_points);
  size_t pos = 0;
  uint64_t probes = 0;
  uint64_t second_hits = 0;

  while (pos + cfg.min_size + cfg.window_w < sample_len && pos < sample_len / 2) {
    const size_t scan_start = pos + cfg.min_size;
    const size_t cut_limit = std::min(pos + cfg.max_size, sample_len);
    if (scan_start >= cut_limit || scan_start < cfg.window_w) break;

    TopoPhResume st{};
    ReplayTo(sample, scan_start, scan_start, cfg.window_w, gear, &st.h);
    st.window_w = cfg.window_w;

    for (size_t p = scan_start; p < cut_limit && p < scan_start + 65536; ++p) {
      ++probes;
      const bool primary_ok = ((st.h & mask) == 0);
      if (primary_ok) {
        if (cfg.kernel == TopoPhKernel::kTriV2) {
          const uint32_t before =
              FlipCountFixed(st.recent_h, st.recent_t, st.recent_count, k_points,
                             cfg.q_mod);
          PushLandmark(st.recent_h, st.recent_t, &st.recent_count, k_points, st.h,
                       static_cast<uint32_t>(p));
          const uint32_t after =
              FlipCountFixed(st.recent_h, st.recent_t, st.recent_count, k_points,
                             cfg.q_mod);
          if (after != before) ++second_hits;
        } else {
          uint32_t cur[kPhH0EpsCount]{};
          PushLandmark(st.recent_h, st.recent_t, &st.recent_count, k_points, st.h,
                       static_cast<uint32_t>(p));
          PhH0BettiVec(st.recent_h, st.recent_t, st.recent_count, k_points,
                       cfg.q_mod, cur);
          if (st.has_prev && PhH0Delta(st.prev_beta0, cur)) ++second_hits;
          std::memcpy(st.prev_beta0, cur, sizeof(cur));
          st.has_prev = true;
        }
      }
      st.h = ((st.h << 1) + gear[sample[p]] - gear[sample[p - cfg.window_w]]) &
             0xFFFFFFFFu;
    }
    pos = cut_limit;
  }

  if (probes == 0) return 500;
  const double p_joint = std::max(
      static_cast<double>(second_hits) / static_cast<double>(probes), 0.01);
  uint16_t lo = 50;
  uint16_t hi = 900;
  uint16_t best = static_cast<uint16_t>(
      std::min(900.0, std::max(50.0, p_joint * 1000.0)));
  auto mean_for = [&](uint16_t perm) -> double {
    TopoPhConfig trial = cfg;
    trial.topo_calib_permille = perm;
    trial.table_seed = seed;
    uint32_t g2[256]{};
    InitGearTable(g2, seed);
    std::vector<size_t> off;
    std::vector<uint32_t> lens;
    if (!CollectChunkCutsTopoPh(sample, sample_len, trial, g2, &off, &lens)
             .ok() ||
        lens.empty()) {
      return 0.0;
    }
    double s = 0.0;
    for (uint32_t l : lens) s += l;
    return s / static_cast<double>(lens.size());
  };
  for (int iter = 0; iter < 10; ++iter) {
    const uint16_t mid = static_cast<uint16_t>((lo + hi) / 2);
    const double mean = mean_for(mid);
    best = mid;
    if (mean <= 0.0) break;
    if (mean < cfg.avg_size * 0.95) {
      lo = static_cast<uint16_t>(mid + 1);  // rarer primary → larger chunks
    } else if (mean > cfg.avg_size * 1.05) {
      hi = mid > 0 ? static_cast<uint16_t>(mid - 1) : 0;
    } else {
      break;
    }
    if (lo > hi) break;
  }
  return best;
}

bool ScanTriV2Cut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                  const TopoPhConfig& cfg, uint32_t mask, const uint32_t gear[256],
                  size_t* out_cut, bool* found, uint64_t* probes) {
  if (!out_cut || !found) return false;
  *found = false;
  if (scan_start >= cut_limit || scan_start < cfg.window_w) return false;
  TopoPhResume st{};
  TopoPhConfig local = cfg;
  local.kernel = TopoPhKernel::kTriV2;
  ReplayTo(data, scan_start, scan_start, cfg.window_w, gear, &st.h);
  for (size_t p = scan_start; p < cut_limit; ++p) {
    if (probes) ++(*probes);
    if (ProbeAt(&st.h, mask, cfg.window_w, gear, local, &st, data, p, out_cut)) {
      *found = true;
      return true;
    }
  }
  return false;
}

bool ScanPhH0Cut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                 const TopoPhConfig& cfg, uint32_t mask, const uint32_t gear[256],
                 size_t* out_cut, bool* found, uint64_t* probes) {
  if (!out_cut || !found) return false;
  *found = false;
  if (scan_start >= cut_limit || scan_start < cfg.window_w) return false;
  TopoPhResume st{};
  TopoPhConfig local = cfg;
  local.kernel = TopoPhKernel::kPhH0;
  ReplayTo(data, scan_start, scan_start, cfg.window_w, gear, &st.h);
  for (size_t p = scan_start; p < cut_limit; ++p) {
    if (probes) ++(*probes);
    if (ProbeAt(&st.h, mask, cfg.window_w, gear, local, &st, data, p, out_cut)) {
      *found = true;
      return true;
    }
  }
  return false;
}

void ClearTopoPhResume(TopoPhResume* resume) {
  if (!resume) return;
  *resume = TopoPhResume{};
  resume->window_w = 64;
}

bool ProcessTopoPhChunk(const uint8_t* data, size_t len, size_t chunk_pos,
                        bool allow_tail_cut, const TopoPhConfig& cfg,
                        uint32_t mask, const uint32_t gear[256],
                        TopoPhResume* resume, size_t* out_cut, bool* chunk_done,
                        uint64_t* probes) {
  if (!resume || !out_cut || !chunk_done) return false;
  *chunk_done = false;
  const uint32_t w = cfg.window_w;
  const size_t scan_start = chunk_pos + cfg.min_size;
  const size_t cut_limit = std::min(chunk_pos + cfg.max_size, len);
  if (scan_start >= cut_limit) {
    if (allow_tail_cut && cut_limit > chunk_pos) {
      *out_cut = cut_limit;
      *chunk_done = true;
      ClearTopoPhResume(resume);
    }
    return true;
  }
  if (scan_start < w) {
    if (allow_tail_cut) {
      *out_cut = cut_limit;
      *chunk_done = true;
      ClearTopoPhResume(resume);
    }
    return true;
  }

  size_t p_start = scan_start;
  if (!resume->in_scan) {
    ClearTopoPhResume(resume);
    resume->window_w = w;
    ReplayTo(data, scan_start, scan_start, w, gear, &resume->h);
    resume->in_scan = true;
    resume->scan_rel = scan_start;
  } else {
    p_start = chunk_pos + resume->scan_rel;
    if (p_start < scan_start) p_start = scan_start;
  }

  for (size_t p = p_start; p < cut_limit; ++p) {
    if (probes) ++(*probes);
    if (ProbeAt(&resume->h, mask, w, gear, cfg, resume, data, p, out_cut)) {
      *chunk_done = true;
      resume->in_scan = false;
      return true;
    }
    resume->scan_rel = p + 1 - chunk_pos;
  }

  if (allow_tail_cut) {
    *out_cut = cut_limit;
    *chunk_done = true;
    ClearTopoPhResume(resume);
  }
  return true;
}

Status CollectChunkCutsTopoPh(const uint8_t* data, size_t len,
                              const TopoPhConfig& cfg, const uint32_t gear[256],
                              std::vector<size_t>* offsets,
                              std::vector<uint32_t>* lengths) {
  if (!offsets || !lengths) return Status::InvalidArgument("null out");
  offsets->clear();
  lengths->clear();
  if (!data && len > 0) return Status::InvalidArgument("null data");
  if (len == 0) return Status::Ok();

  uint32_t mask = 0;
  BuildPhMasks(cfg.avg_size, cfg.topo_calib_permille, cfg.topo_shift, &mask);

  size_t pos = 0;
  while (pos < len) {
    const size_t remain = len - pos;
    if (remain <= cfg.min_size) {
      offsets->push_back(pos);
      lengths->push_back(static_cast<uint32_t>(remain));
      break;
    }
    const size_t scan_start = pos + cfg.min_size;
    const size_t cut_limit = std::min(pos + cfg.max_size, len);
    size_t cut = cut_limit;
    bool found = false;
    if (cfg.kernel == TopoPhKernel::kPhH0) {
      ScanPhH0Cut(data, scan_start, cut_limit, cfg, mask, gear, &cut, &found,
                  nullptr);
    } else {
      ScanTriV2Cut(data, scan_start, cut_limit, cfg, mask, gear, &cut, &found,
                   nullptr);
    }
    if (!found) cut = cut_limit;
    if (cut <= pos) cut = std::min(pos + cfg.min_size, len);
    offsets->push_back(pos);
    lengths->push_back(static_cast<uint32_t>(cut - pos));
    pos = cut;
  }
  return Status::Ok();
}

}  // namespace topo_ph_internal
}  // namespace ebbackup
