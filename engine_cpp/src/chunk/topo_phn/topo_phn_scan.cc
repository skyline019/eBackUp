#include "ebbackup/chunk/topo_phn_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#if defined(__AVX2__) || defined(_M_AVX2)
#include <immintrin.h>
#endif

namespace ebbackup {
namespace topo_phn_internal {

namespace {

constexpr uint32_t kLcgM = 1664525u;
constexpr uint32_t kLcgC = 1013904223u;
// Closed-form weights for 4 LCG steps (uint32 wrap = iterative LCG).
constexpr uint32_t kLcgM2 = kLcgM * kLcgM;
constexpr uint32_t kLcgM3 = kLcgM2 * kLcgM;
constexpr uint32_t kLcgM4 = kLcgM3 * kLcgM;
constexpr uint32_t kLcgCGeom =
    kLcgC * (1u + kLcgM + kLcgM2 + kLcgM3);

// Full-window embed for p>=3: bit-identical to 4 iterative LCG steps + XOR.
inline uint32_t LandmarkEmbedY4(uint32_t seed, uint8_t b0, uint8_t b1,
                                uint8_t b2, uint8_t b3) {
  uint32_t y = seed * kLcgM4 + kLcgCGeom;
  y += static_cast<uint32_t>(b0) * kLcgM3;
  y += static_cast<uint32_t>(b1) * kLcgM2;
  y += static_cast<uint32_t>(b2) * kLcgM;
  y += static_cast<uint32_t>(b3);
  y ^= static_cast<uint32_t>(b2) << 8;
  y ^= static_cast<uint32_t>(b1) << 16;
  y ^= static_cast<uint32_t>(b0) << 24;
  return y;
}

// Nested ε² grid (3 levels): denser→sparser; AND-gate Δβ0 via ∃j.
constexpr uint32_t kEpsR2[kPhnH0EpsCount] = {1u, 4u, 16u};

inline int64_t CrossI(int64_t ax, int64_t ay, int64_t bx, int64_t by) {
  return ax * by - ay * bx;
}

inline uint32_t LandmarkIndex(uint32_t i, uint32_t count, uint8_t k_points,
                              uint8_t ring_start) {
  if (count < k_points) return i;
  return (static_cast<uint32_t>(ring_start) + i) % k_points;
}

}  // namespace

void PushLandmark(uint32_t* recent_y, uint32_t* recent_t, uint32_t* count,
                  uint8_t* ring_start, uint8_t k_points, uint32_t y,
                  uint32_t t) {
  if (*count < k_points) {
    recent_y[*count] = y;
    recent_t[*count] = t;
    ++(*count);
    return;
  }
  // Overwrite oldest; advance ring so start points at new oldest.
  recent_y[*ring_start] = y;
  recent_t[*ring_start] = t;
  *ring_start = static_cast<uint8_t>((*ring_start + 1) % k_points);
}

bool MinGapOk(const TopoPhnResume& st, size_t abs_pos, uint32_t min_size) {
  if (!st.has_last_cut) return true;
  return abs_pos >= st.last_cut_abs + min_size;
}

// Content-local embed: seed + causal neighborhood (no cumulative stream LCG).
uint32_t LandmarkEmbedY(const uint8_t* data, size_t /*len*/, size_t p,
                        uint32_t table_seed) {
  if (p >= 3) {
    return LandmarkEmbedY4(table_seed, data[p - 3], data[p - 2], data[p - 1],
                           data[p]);
  }
  uint32_t y = table_seed;
  for (size_t i = 0; i <= p; ++i) {
    y = y * kLcgM + kLcgC + data[i];
  }
  if (p >= 1) y ^= static_cast<uint32_t>(data[p - 1]) << 8;
  if (p >= 2) y ^= static_cast<uint32_t>(data[p - 2]) << 16;
  return y;
}

bool EvalEventAtLandmark(const TopoPhnConfig& cfg, TopoPhnResume* st, uint32_t y,
                         uint32_t t_abs) {
  const uint8_t k_points = ClampKPoints(cfg.k_points);
  if (cfg.kernel == TopoPhnKernel::kTriNative) {
    PushLandmark(st->recent_y, st->recent_t, &st->recent_count, &st->ring_start,
                 k_points, y, t_abs);
    const uint32_t after =
        FlipCountFixed(st->recent_y, st->recent_t, st->recent_count, k_points,
                       cfg.q_mod, st->ring_start);
    bool ok = false;
    if (st->has_prev) {
      const uint32_t delta =
          after > st->prev_flip ? after - st->prev_flip : st->prev_flip - after;
      const uint32_t tau = cfg.flip_tau == 0 ? 1 : cfg.flip_tau;
      ok = delta >= tau;
    }
    st->prev_flip = after;
    st->has_prev = true;
    return ok;
  }

  // PH-H0-Native: cut = flipOK ∧ ph0OK 〔∧ persist when enabled〕.
  uint32_t before[kPhnH0EpsCount]{};
  if (st->recent_count > 0) {
    PhH0BettiVec(st->recent_y, st->recent_t, st->recent_count, k_points,
                 cfg.q_mod, before, st->ring_start);
  }
  PushLandmark(st->recent_y, st->recent_t, &st->recent_count, &st->ring_start,
               k_points, y, t_abs);
  uint32_t cur[kPhnH0EpsCount]{};
  PhH0BettiVec(st->recent_y, st->recent_t, st->recent_count, k_points, cfg.q_mod,
               cur, st->ring_start);
  bool ph_ok = false;
  if (st->recent_count > 1) {
    ph_ok = PhH0Delta(before, cur);
    if (ph_ok && cfg.enable_persist_delta) {
      const uint8_t span = PhH0PersistSpan(cur);
      ph_ok = span >= cfg.persist_delta;
    }
  }
  bool flip_ok = false;
  if (st->recent_count >= 4) {
    const uint32_t flips =
        FlipCountFixed(st->recent_y, st->recent_t, st->recent_count, k_points,
                       cfg.q_mod, st->ring_start);
    if (st->has_prev) {
      const uint32_t d =
          flips > st->prev_flip ? flips - st->prev_flip : st->prev_flip - flips;
      const uint32_t tau = cfg.flip_tau == 0 ? 1 : cfg.flip_tau;
      flip_ok = d >= tau;
    }
    st->prev_flip = flips;
  }
  std::memcpy(st->prev_beta0, cur, sizeof(cur));
  st->has_prev = true;
  return flip_ok && ph_ok;
}

void FillPhnCalibSample(uint8_t* out, size_t len, uint32_t seed) {
  if (!out || len == 0) return;
  uint32_t s = seed ? seed : 0xC0FFEEu;
  for (size_t i = 0; i < len; ++i) {
    s = s * 1664525u + 1013904223u;
    out[i] = static_cast<uint8_t>((s >> 16) & 0xFFu);
  }
}

uint32_t FlipCountFixed(const uint32_t* recent_y, const uint32_t* recent_t,
                        uint32_t count, uint8_t k_points, uint32_t q_mod,
                        uint8_t ring_start) {
  if (!recent_y || !recent_t || count < 4 || k_points == 0 || q_mod == 0) {
    return 0;
  }
  const uint32_t k = std::min(count, static_cast<uint32_t>(k_points));
  auto Y = [&](uint32_t i) {
    return recent_y[LandmarkIndex(i, k, k_points, ring_start)];
  };
  auto T = [&](uint32_t i) {
    return recent_t[LandmarkIndex(i, k, k_points, ring_start)];
  };
  uint32_t flips = 0;
  for (uint32_t i = 1; i + 1 < k; ++i) {
    const int64_t ax = static_cast<int64_t>(T(0) % q_mod);
    const int64_t ay = static_cast<int64_t>(Y(0) % q_mod);
    const int64_t bx = static_cast<int64_t>(T(i) % q_mod);
    const int64_t by = static_cast<int64_t>(Y(i) % q_mod);
    const int64_t cx = static_cast<int64_t>(T(i + 1) % q_mod);
    const int64_t cy = static_cast<int64_t>(Y(i + 1) % q_mod);
    if (CrossI(bx - ax, by - ay, cx - ax, cy - ay) <= 0) ++flips;
  }
  for (uint32_t i = 0; i + 2 < k; ++i) {
    const int64_t ax = static_cast<int64_t>(T(i) % q_mod);
    const int64_t ay = static_cast<int64_t>(Y(i) % q_mod);
    const int64_t bx = static_cast<int64_t>(T(i + 1) % q_mod);
    const int64_t by = static_cast<int64_t>(Y(i + 1) % q_mod);
    const int64_t cx = static_cast<int64_t>(T(i + 2) % q_mod);
    const int64_t cy = static_cast<int64_t>(Y(i + 2) % q_mod);
    if (CrossI(bx - ax, by - ay, cx - ax, cy - ay) <= 0) ++flips;
  }
  return flips;
}

void PhH0BettiVec(const uint32_t* recent_y, const uint32_t* recent_t,
                  uint32_t count, uint8_t k_points, uint32_t q_mod,
                  uint32_t out_beta0[kPhnH0EpsCount], uint8_t ring_start) {
  const uint8_t kp = ClampKPoints(k_points);
  const uint32_t k = std::min(count, static_cast<uint32_t>(kp));
  if (k == 0) {
    for (size_t i = 0; i < kPhnH0EpsCount; ++i) out_beta0[i] = 0;
    return;
  }

  // Nested UF: process edges once in increasing d2, snapshot β0 at each ε.
  struct Edge {
    uint8_t a;
    uint8_t b;
    uint32_t d2;
  };
  Edge edges[kTopoPhnMaxK * kTopoPhnMaxK / 2];
  uint32_t n_edges = 0;
  for (uint32_t i = 0; i < k; ++i) {
    const uint32_t ii = LandmarkIndex(i, k, kp, ring_start);
    const int64_t xi = static_cast<int64_t>(recent_t[ii] % q_mod);
    const int64_t yi = static_cast<int64_t>(recent_y[ii] % q_mod);
    for (uint32_t j = i + 1; j < k; ++j) {
      const uint32_t jj = LandmarkIndex(j, k, kp, ring_start);
      const int64_t xj = static_cast<int64_t>(recent_t[jj] % q_mod);
      const int64_t yj = static_cast<int64_t>(recent_y[jj] % q_mod);
      const int64_t dx = xi - xj;
      const int64_t dy = yi - yj;
      edges[n_edges++] = {static_cast<uint8_t>(i), static_cast<uint8_t>(j),
                          static_cast<uint32_t>(dx * dx + dy * dy)};
    }
  }
  std::sort(edges, edges + n_edges,
            [](const Edge& a, const Edge& b) { return a.d2 < b.d2; });

  uint8_t parent[kTopoPhnMaxK];
  for (uint32_t i = 0; i < k; ++i) parent[i] = static_cast<uint8_t>(i);
  auto find = [&](uint8_t x) {
    while (parent[x] != x) x = parent[x];
    return x;
  };
  auto unite = [&](uint8_t a, uint8_t b) -> bool {
    a = find(a);
    b = find(b);
    if (a == b) return false;
    parent[a] = b;
    return true;
  };

  uint32_t comps = k;
  uint32_t e = 0;
  for (size_t ei = 0; ei < kPhnH0EpsCount; ++ei) {
    const uint32_t r2 = kEpsR2[ei];
    while (e < n_edges && edges[e].d2 <= r2) {
      if (unite(edges[e].a, edges[e].b)) --comps;
      ++e;
    }
    out_beta0[ei] = comps;
  }
}

bool PhH0Delta(const uint32_t* prev, const uint32_t* cur) {
  if (!prev || !cur) return false;
  for (size_t i = 0; i < kPhnH0EpsCount; ++i) {
    if (prev[i] != cur[i]) return true;
  }
  return false;
}

uint8_t PhH0PersistSpan(const uint32_t* beta0) {
  if (!beta0) return 0;
  uint32_t mn = beta0[0];
  uint32_t mx = beta0[0];
  for (size_t i = 1; i < kPhnH0EpsCount; ++i) {
    mn = std::min(mn, beta0[i]);
    mx = std::max(mx, beta0[i]);
  }
  const uint32_t span = mx > mn ? mx - mn : 0;
  return static_cast<uint8_t>(std::min<uint32_t>(span, 255u));
}

void ClearTopoPhnResume(TopoPhnResume* resume) {
  if (!resume) return;
  *resume = TopoPhnResume{};
}

// Drop Flip/H0 landmark history after a cut so the next chunk does not inherit
// a pre-insert ring (which blocks 1-byte boundary re-lock).
void ResetLandmarkWindow(TopoPhnResume* resume) {
  if (!resume) return;
  resume->recent_count = 0;
  resume->ring_start = 0;
  resume->has_prev = false;
  resume->prev_flip = 0;
  std::memset(resume->prev_beta0, 0, sizeof(resume->prev_beta0));
  resume->bytes_since_lm = 0;
  resume->in_scan = false;
  resume->scan_rel = 0;
}

bool ProcessPhnChunk(const uint8_t* data, size_t len, size_t chunk_pos,
                     bool allow_tail_cut, const TopoPhnConfig& cfg,
                     TopoPhnResume* resume, size_t* out_cut, bool* chunk_done,
                     uint64_t* probes, size_t abs_base) {
  if (!resume || !out_cut || !chunk_done) return false;
  *chunk_done = false;
  const uint32_t stride = RoundEventStridePow2(cfg.event_stride);
  const uint32_t stride_mask = stride > 1 ? (stride - 1u) : 0u;
  const size_t scan_start = chunk_pos + cfg.min_size;
  const size_t cut_limit = std::min(chunk_pos + cfg.max_size, len);
  const uint32_t seed = cfg.table_seed;

  if (scan_start >= cut_limit) {
    if (allow_tail_cut && cut_limit > chunk_pos) {
      *out_cut = cut_limit;
      *chunk_done = true;
      resume->has_last_cut = true;
      resume->last_cut_abs = abs_base + cut_limit;
      ResetLandmarkWindow(resume);
    }
    return true;
  }

  size_t p = scan_start;
  if (!resume->in_scan) {
    resume->in_scan = true;
    resume->scan_rel = scan_start - chunk_pos;
  } else {
    p = chunk_pos + resume->scan_rel;
    if (p < scan_start) p = scan_start;
  }

  auto try_landmark = [&](size_t pos, uint32_t y) -> bool {
    const uint32_t t_rel = static_cast<uint32_t>(pos - chunk_pos);
    const size_t abs_p = abs_base + pos;
    if (!EvalEventAtLandmark(cfg, resume, y, t_rel)) return false;
    if (!MinGapOk(*resume, abs_p, cfg.min_size)) return false;
    *out_cut = pos;
    *chunk_done = true;
    resume->has_last_cut = true;
    resume->last_cut_abs = abs_p;
    ResetLandmarkWindow(resume);
    return true;
  };

  // Prefer EmbedY4 (p>=3). min_size is always >>3 for Default/Large/Small.
  while (p < cut_limit && p < 3) {
    if (probes) ++(*probes);
    const uint32_t y = LandmarkEmbedY(data, len, p, seed);
    if (stride_mask == 0 || (y & stride_mask) == 0) {
      if (try_landmark(p, y)) return true;
    }
    resume->scan_rel = p + 1 - chunk_pos;
    ++p;
  }

#if defined(__AVX2__) || defined(_M_AVX2)
  // Closed-form EmbedY4 ×8 (bit-identical); 16B step = two packs; skip store if no hit.
  const __m256i vSeed = _mm256_set1_epi32(static_cast<int>(seed));
  const __m256i vM4 = _mm256_set1_epi32(static_cast<int>(kLcgM4));
  const __m256i vM3 = _mm256_set1_epi32(static_cast<int>(kLcgM3));
  const __m256i vM2 = _mm256_set1_epi32(static_cast<int>(kLcgM2));
  const __m256i vM1 = _mm256_set1_epi32(static_cast<int>(kLcgM));
  const __m256i vCGeom = _mm256_set1_epi32(static_cast<int>(kLcgCGeom));
  const __m256i vMask = _mm256_set1_epi32(static_cast<int>(stride_mask));
  const __m256i vSeedTerm =
      _mm256_add_epi32(_mm256_mullo_epi32(vSeed, vM4), vCGeom);

  auto embed8_at = [&](size_t base, __m256i* out_y) {
    const __m256i vb0 = _mm256_cvtepu8_epi32(
        _mm_loadl_epi64(reinterpret_cast<const __m128i*>(data + base - 3)));
    const __m256i vb1 = _mm256_cvtepu8_epi32(
        _mm_loadl_epi64(reinterpret_cast<const __m128i*>(data + base - 2)));
    const __m256i vb2 = _mm256_cvtepu8_epi32(
        _mm_loadl_epi64(reinterpret_cast<const __m128i*>(data + base - 1)));
    const __m256i vb3 = _mm256_cvtepu8_epi32(
        _mm_loadl_epi64(reinterpret_cast<const __m128i*>(data + base)));
    __m256i y = vSeedTerm;
    y = _mm256_add_epi32(y, _mm256_mullo_epi32(vb0, vM3));
    y = _mm256_add_epi32(y, _mm256_mullo_epi32(vb1, vM2));
    y = _mm256_add_epi32(y, _mm256_mullo_epi32(vb2, vM1));
    y = _mm256_add_epi32(y, vb3);
    y = _mm256_xor_si256(y, _mm256_slli_epi32(vb2, 8));
    y = _mm256_xor_si256(y, _mm256_slli_epi32(vb1, 16));
    y = _mm256_xor_si256(y, _mm256_slli_epi32(vb0, 24));
    *out_y = y;
  };

  auto hit_mask = [&](const __m256i& y) -> int {
    if (stride_mask == 0) return 0xFF;
    const __m256i tand = _mm256_and_si256(y, vMask);
    const __m256i eq0 =
        _mm256_cmpeq_epi32(tand, _mm256_setzero_si256());
    return _mm256_movemask_ps(_mm256_castsi256_ps(eq0));
  };

  auto apply_hits = [&](size_t base, const __m256i& y, int bits) -> bool {
    if (bits == 0) return false;
    alignas(32) uint32_t ys[8];
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(ys), y);
    for (int j = 0; j < 8; ++j) {
      if ((bits & (1 << j)) == 0) continue;
      if (try_landmark(base + static_cast<size_t>(j), ys[j])) return true;
    }
    return false;
  };

  while (p + 16 <= cut_limit) {
    _mm_prefetch(reinterpret_cast<const char*>(data + p + 64), _MM_HINT_T0);
    __m256i y0, y1;
    embed8_at(p, &y0);
    embed8_at(p + 8, &y1);
    if (probes) *probes += 16;
    const int bits0 = hit_mask(y0);
    const int bits1 = hit_mask(y1);
    if (bits0 && apply_hits(p, y0, bits0)) return true;
    if (bits1 && apply_hits(p + 8, y1, bits1)) return true;
    resume->scan_rel = p + 16 - chunk_pos;
    p += 16;
  }

  while (p + 8 <= cut_limit) {
    _mm_prefetch(reinterpret_cast<const char*>(data + p + 64), _MM_HINT_T0);
    __m256i y;
    embed8_at(p, &y);
    if (probes) *probes += 8;
    const int bits = hit_mask(y);
    if (bits && apply_hits(p, y, bits)) return true;
    resume->scan_rel = p + 8 - chunk_pos;
    p += 8;
  }
#endif

  // Scalar / remainder: closed-form EmbedY4 (bit-identical to 4-step LCG).
  while (p < cut_limit) {
    if (probes) ++(*probes);
    const uint32_t y =
        LandmarkEmbedY4(seed, data[p - 3], data[p - 2], data[p - 1], data[p]);
    if (stride_mask == 0 || (y & stride_mask) == 0) {
      if (try_landmark(p, y)) return true;
    }
    resume->scan_rel = p + 1 - chunk_pos;
    ++p;
  }

  if (allow_tail_cut) {
    *out_cut = cut_limit;
    *chunk_done = true;
    resume->has_last_cut = true;
    resume->last_cut_abs = abs_base + cut_limit;
    ResetLandmarkWindow(resume);
  }
  return true;
}

Status CollectChunkCutsTopoPhn(const uint8_t* data, size_t len,
                               const TopoPhnConfig& cfg,
                               std::vector<size_t>* offsets,
                               std::vector<uint32_t>* lengths) {
  if (!offsets || !lengths) return Status::InvalidArgument("null out");
  offsets->clear();
  lengths->clear();
  if (!data && len > 0) return Status::InvalidArgument("null data");
  if (len == 0) return Status::Ok();

  TopoPhnResume st{};
  st.rolling_mix = cfg.table_seed;
  size_t pos = 0;
  while (pos < len) {
    const size_t remain = len - pos;
    if (remain <= cfg.min_size) {
      offsets->push_back(pos);
      lengths->push_back(static_cast<uint32_t>(remain));
      break;
    }
    size_t cut = 0;
    bool done = false;
    if (!ProcessPhnChunk(data, len, pos, true, cfg, &st, &cut, &done, nullptr,
                         0) ||
        !done || cut <= pos) {
      cut = std::min(pos + cfg.max_size, len);
      if (cut <= pos) cut = len;
      ClearTopoPhnResume(&st);
      st.rolling_mix = cfg.table_seed;
    }
    offsets->push_back(pos);
    lengths->push_back(static_cast<uint32_t>(cut - pos));
    pos = cut;
  }
  return Status::Ok();
}

uint32_t CalibrateEventStride(const uint8_t* sample, size_t sample_len,
                              const TopoPhnConfig& cfg) {
  TopoPhnConfig mut = cfg;
  CalibratePhnCutParams(sample, sample_len, &mut);
  return RoundEventStridePow2(mut.event_stride);
}

namespace {

double MeanForConfig(const uint8_t* sample, size_t sample_len,
                     const TopoPhnConfig& cfg) {
  std::vector<size_t> off;
  std::vector<uint32_t> lens;
  if (!CollectChunkCutsTopoPhn(sample, sample_len, cfg, &off, &lens).ok() ||
      lens.empty()) {
    return 0.0;
  }
  double s = 0.0;
  for (uint32_t l : lens) s += l;
  return s / static_cast<double>(lens.size());
}

void BinarySearchTau(const uint8_t* sample, size_t sample_len,
                     TopoPhnConfig* cfg) {
  uint32_t lo = 1;
  uint32_t hi = 512;
  uint32_t best = cfg->flip_tau == 0 ? 1 : cfg->flip_tau;
  double best_err = 1e300;
  for (int iter = 0; iter < 16; ++iter) {
    const uint32_t mid = (lo + hi) / 2;
    cfg->flip_tau = mid == 0 ? 1 : mid;
    const double mean = MeanForConfig(sample, sample_len, *cfg);
    if (mean <= 0.0) break;
    const double err = std::abs(mean - static_cast<double>(cfg->avg_size));
    if (err < best_err) {
      best_err = err;
      best = cfg->flip_tau;
    }
    if (mean >= cfg->avg_size * 0.95 && mean <= cfg->avg_size * 1.05) {
      best = cfg->flip_tau;
      break;
    }
    if (mean < cfg->avg_size * 0.95) {
      lo = mid + 1;
    } else {
      hi = mid > 1 ? mid - 1 : 1;
    }
    if (lo > hi) break;
  }
  cfg->flip_tau = best == 0 ? 1 : best;
}

void CalibrateTauOrPersist(const uint8_t* sample, size_t sample_len,
                           TopoPhnConfig* cfg) {
  if (!cfg || !sample || sample_len < cfg->min_size + 1024) return;
  BinarySearchTau(sample, sample_len, cfg);
}

void CalibrateStrideOnly(const uint8_t* sample, size_t sample_len,
                         TopoPhnConfig* cfg, uint32_t stride_lo) {
  if (!cfg || !sample || sample_len < cfg->min_size + 1024) return;
  // Search the power-of-two lattice only (hot path masks with stride-1).
  uint32_t floor = RoundEventStridePow2(stride_lo);
  if (floor < kMinEventStride) floor = kMinEventStride;
  uint32_t best = RoundEventStridePow2(std::max(cfg->event_stride, floor));
  double best_err = 1e300;
  for (uint32_t s = floor; s <= kMaxEventStride; ) {
    cfg->event_stride = s;
    const double mean = MeanForConfig(sample, sample_len, *cfg);
    if (mean > 0.0) {
      const double err = std::abs(mean - static_cast<double>(cfg->avg_size));
      if (err < best_err) {
        best_err = err;
        best = s;
      }
      if (mean >= cfg->avg_size * 0.95 && mean <= cfg->avg_size * 1.05) {
        best = s;
        break;
      }
    }
    if (s > (kMaxEventStride >> 1)) break;
    s <<= 1;
  }
  cfg->event_stride = best;
}

// Densify when mean too high: τ → k → stride (never jump to 8 first).
void DensifyForHighMean(const uint8_t* sample, size_t sample_len,
                        TopoPhnConfig* cfg) {
  const uint32_t soft_floor =
      cfg->kernel == TopoPhnKernel::kPhH0Native
          ? PhnProductiveStrideFloor(cfg->avg_size)
          : ClampEventStride(std::max(32u, cfg->avg_size / 256u));

  // 1) Lower τ
  if (cfg->flip_tau > 1) {
    cfg->flip_tau = cfg->flip_tau / 2;
    CalibrateTauOrPersist(sample, sample_len, cfg);
    if (MeanForConfig(sample, sample_len, *cfg) <= cfg->avg_size * 1.15) return;
  }
  cfg->flip_tau = 1;
  CalibrateTauOrPersist(sample, sample_len, cfg);
  if (MeanForConfig(sample, sample_len, *cfg) <= cfg->avg_size * 1.15) return;

  // 2) Drop k (keep persist so PH≠Tri)
  for (uint8_t k = ClampKPoints(cfg->k_points); k >= 4;
       k = static_cast<uint8_t>(k <= 4 ? 4 : k - 2)) {
    cfg->k_points = k;
    CalibrateTauOrPersist(sample, sample_len, cfg);
    if (MeanForConfig(sample, sample_len, *cfg) <= cfg->avg_size * 1.15) return;
    if (k == 4) break;
  }

  // 3) Halve stride down to soft floor (stay on pow2 lattice)
  for (int i = 0; i < 8; ++i) {
    if (cfg->event_stride <= soft_floor) break;
    const uint32_t cur = RoundEventStridePow2(cfg->event_stride);
    const uint32_t half =
        cur > soft_floor ? RoundEventStridePow2(std::max(cur >> 1, soft_floor))
                         : soft_floor;
    if (half >= cur) break;
    cfg->event_stride = half;
    CalibrateTauOrPersist(sample, sample_len, cfg);
    if (MeanForConfig(sample, sample_len, *cfg) <= cfg->avg_size * 1.15) return;
  }

  // 4) Last resort toward kMin only when τ=1 and k=4
  if (cfg->flip_tau <= 1 && ClampKPoints(cfg->k_points) <= 4) {
    if (cfg->kernel == TopoPhnKernel::kPhH0Native &&
        cfg->enable_persist_delta) {
      cfg->enable_persist_delta = false;
      CalibrateTauOrPersist(sample, sample_len, cfg);
      if (MeanForConfig(sample, sample_len, *cfg) <= cfg->avg_size * 1.15) return;
    }
    CalibrateStrideOnly(sample, sample_len, cfg, kMinEventStride);
    CalibrateTauOrPersist(sample, sample_len, cfg);
  }
}

void SparsifyForLowMean(const uint8_t* sample, size_t sample_len,
                        TopoPhnConfig* cfg) {
  // Prefer widening stride (primary Tri rate knob). Avoid BinarySearchTau
  // overwriting intentional bump when ΔFlip is nearly binary.
  for (int i = 0; i < 10; ++i) {
    const double mean = MeanForConfig(sample, sample_len, *cfg);
    if (mean <= 0.0 || mean >= cfg->avg_size * 0.85) break;
    if (cfg->event_stride >= kMaxEventStride) {
      if (cfg->flip_tau < 512) {
        cfg->flip_tau = std::min<uint32_t>(cfg->flip_tau * 2 + 1, 512);
      } else {
        break;
      }
    } else {
      const uint32_t cur = RoundEventStridePow2(cfg->event_stride);
      const uint32_t next =
          cur > (kMaxEventStride >> 1) ? kMaxEventStride : (cur << 1);
      cfg->event_stride = next;
    }
  }
  CalibrateStrideOnly(sample, sample_len, cfg,
                      RoundEventStridePow2(cfg->event_stride));
}

}  // namespace

void CalibratePhnCutParams(const uint8_t* sample, size_t sample_len,
                           TopoPhnConfig* cfg) {
  if (!cfg) return;
  if (!sample || sample_len < cfg->min_size + 1024) {
    cfg->event_stride = RoundEventStridePow2(cfg->event_stride);
    return;
  }

  if (cfg->kernel == TopoPhnKernel::kPhH0Native) {
    if (cfg->k_points < 16) cfg->k_points = 16;
    // Persist delta keeps PH AND-gate from collapsing to Tri-identical cuts
    // when landmarks are sparse (pow2 stride lattice).
    cfg->enable_persist_delta = true;
    if (cfg->persist_delta == 0) cfg->persist_delta = 1;
  } else if (cfg->k_points == 0 || cfg->k_points > 16) {
    cfg->k_points = 8;
  } else if (cfg->k_points == 16) {
    cfg->k_points = 8;
  }

  const uint32_t hint_div =
      cfg->kernel == TopoPhnKernel::kPhH0Native ? 128u : 64u;
  const uint32_t hint =
      cfg->avg_size > hint_div ? static_cast<uint32_t>(cfg->avg_size / hint_div)
                               : 32;
  cfg->event_stride = RoundEventStridePow2(hint);

  // Tri ΔFlip is typically 0/1 → rate-control mainly via stride (τ≈1).
  if (cfg->kernel == TopoPhnKernel::kTriNative) {
    cfg->flip_tau = 1;
    CalibrateStrideOnly(sample, sample_len, cfg, kMinEventStride);
    for (int guard = 0; guard < 10; ++guard) {
      const double mean = MeanForConfig(sample, sample_len, *cfg);
      if (mean <= 0.0) break;
      if (mean >= cfg->avg_size * 0.85 && mean <= cfg->avg_size * 1.15) break;
      if (mean < cfg->avg_size * 0.85) {
        SparsifyForLowMean(sample, sample_len, cfg);
      } else {
        if (cfg->event_stride > kMinEventStride) {
          cfg->event_stride = RoundEventStridePow2(
              std::max(cfg->event_stride / 2u,
                       static_cast<uint32_t>(kMinEventStride)));
        } else if (cfg->flip_tau > 1) {
          cfg->flip_tau = cfg->flip_tau / 2;
        }
        CalibrateStrideOnly(sample, sample_len, cfg, kMinEventStride);
      }
    }
    cfg->event_stride = RoundEventStridePow2(cfg->event_stride);
    return;
  }

  CalibrateTauOrPersist(sample, sample_len, cfg);

  double mean = MeanForConfig(sample, sample_len, *cfg);
  if (mean > 0.0 && mean < cfg->avg_size * 0.85) {
    SparsifyForLowMean(sample, sample_len, cfg);
  } else if (mean > cfg->avg_size * 1.15) {
    DensifyForHighMean(sample, sample_len, cfg);
  }

  for (int guard = 0; guard < 10; ++guard) {
    mean = MeanForConfig(sample, sample_len, *cfg);
    if (mean <= 0.0) break;
    if (mean >= cfg->avg_size * 0.85 && mean <= cfg->avg_size * 1.15) break;
    if (mean < cfg->avg_size * 0.85) {
      SparsifyForLowMean(sample, sample_len, cfg);
    } else {
      DensifyForHighMean(sample, sample_len, cfg);
    }
  }
  cfg->event_stride = RoundEventStridePow2(cfg->event_stride);
}

void CalibratePhnRuntimeParams(const uint8_t* sample, size_t sample_len,
                               TopoPhnConfig* cfg) {
  if (!cfg) return;
  // Scaled SB stride is a floor: runtime may sparsify further for mean banding.
  const uint32_t stride_floor = RoundEventStridePow2(cfg->event_stride);
  const uint8_t fixed_k = ClampKPoints(cfg->k_points);
  const bool fixed_persist = cfg->enable_persist_delta;
  const uint8_t fixed_pd = cfg->persist_delta;
  cfg->k_points = fixed_k;
  cfg->enable_persist_delta = fixed_persist;
  cfg->persist_delta = fixed_pd;
  cfg->event_stride = stride_floor;

  if (!sample || sample_len < cfg->min_size + 1024) {
    CalibrateTauOrPersist(sample, sample_len, cfg);
    cfg->event_stride = stride_floor;
    return;
  }

  // Tri ΔFlip is typically 0/1 → rate-control via stride (τ stays 1).
  if (cfg->kernel == TopoPhnKernel::kTriNative) {
    cfg->flip_tau = 1;
    cfg->event_stride = stride_floor;
    CalibrateStrideOnly(sample, sample_len, cfg, stride_floor);
    cfg->event_stride = RoundEventStridePow2(cfg->event_stride);
    return;
  }

  CalibrateTauOrPersist(sample, sample_len, cfg);
  cfg->event_stride = stride_floor;
  cfg->k_points = fixed_k;
  cfg->enable_persist_delta = fixed_persist;
  cfg->persist_delta = fixed_pd;

  for (int guard = 0; guard < 8; ++guard) {
    const double mean = MeanForConfig(sample, sample_len, *cfg);
    if (mean <= 0.0) break;
    if (mean >= cfg->avg_size * 0.85 && mean <= cfg->avg_size * 1.15) break;
    if (mean < cfg->avg_size * 0.85) {
      SparsifyForLowMean(sample, sample_len, cfg);
      if (cfg->event_stride < stride_floor) cfg->event_stride = stride_floor;
      continue;
    }
    if (cfg->event_stride > stride_floor) {
      cfg->event_stride = RoundEventStridePow2(
          std::max(cfg->event_stride / 2u, stride_floor));
      CalibrateTauOrPersist(sample, sample_len, cfg);
      continue;
    }
    if (cfg->flip_tau > 1) {
      cfg->flip_tau = cfg->flip_tau / 2;
      CalibrateTauOrPersist(sample, sample_len, cfg);
      continue;
    }
    break;
  }
  if (cfg->event_stride < stride_floor) cfg->event_stride = stride_floor;
  cfg->event_stride = RoundEventStridePow2(cfg->event_stride);
}

}  // namespace topo_phn_internal
}  // namespace ebbackup
