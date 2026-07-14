#include "ebbackup/chunk/topo_cdc_internal.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "ebbackup/chunk/gt_cdc_internal.h"

#if defined(__AVX2__) || defined(_M_AVX2)
#include <immintrin.h>
#endif

namespace ebbackup {
namespace topo_cdc_internal {

namespace {

uint8_t TopoKey(const uint32_t gear[256], uint8_t byte) {
  return static_cast<uint8_t>(gear[byte] & 0xFFu);
}

uint8_t FindSlot(uint8_t parent[kTopoMaxWindow], uint8_t x) {
  while (parent[x] != x) {
    parent[x] = parent[parent[x]];
    x = parent[x];
  }
  return x;
}

bool UnionSlots(uint8_t parent[kTopoMaxWindow], uint8_t rank[kTopoMaxWindow],
                uint8_t a, uint8_t b) {
  uint8_t ra = FindSlot(parent, a);
  uint8_t rb = FindSlot(parent, b);
  if (ra == rb) return false;
  if (rank[ra] < rank[rb]) {
    const uint8_t t = ra;
    ra = rb;
    rb = t;
  }
  parent[rb] = ra;
  if (rank[ra] == rank[rb]) ++rank[ra];
  return true;
}

double MeanChunkLenHom(const uint8_t* data, size_t len, const TopoCdcConfig& cfg,
                       uint16_t calib_permille, const uint32_t gear[256]);

}  // namespace

namespace {

uint32_t EdgeDiff(const uint8_t* keys, uint32_t a, uint32_t b) {
  return keys[a] != keys[b] ? 1u : 0u;
}

}  // namespace

uint32_t ApplySlideEdgeDiffO1(const SlotUfWindow& uf, uint8_t key_in,
                              uint32_t head_old, uint8_t old_tail_key) {
  const uint32_t front0 = head_old;
  const uint32_t front1 = (head_old + 1) % uf.w;
  uint32_t ed = uf.edge_diff;
  if (EdgeDiff(uf.key, front0, front1)) --ed;
  if (uf.w == 2) {
    const uint32_t other = front1;
    if (uf.key[other] != key_in) ++ed;
    return ed;
  }
  const uint32_t penult = (head_old + uf.w - 2) % uf.w;
  if (uf.key[penult] != old_tail_key) --ed;
  if (uf.key[penult] != key_in) ++ed;
  if (key_in != uf.key[head_old]) ++ed;
  return ed;
}

uint32_t ComponentsAfterSlide(uint32_t edge_diff, uint8_t key_in,
                              uint8_t head_old_key, uint32_t w) {
  (void)key_in;
  (void)head_old_key;
  (void)w;
  return 1u + edge_diff;
}

namespace {

inline uint32_t SlotIndex(uint32_t wmask, uint32_t w, uint32_t i) {
  return wmask ? (i & wmask) : (i % w);
}

inline int32_t PrimaryTopoDelta(const uint8_t* keys, uint32_t w, uint8_t key_in,
                                uint32_t ed_old) {
  uint32_t ed_new = ed_old;
  if (keys[0] != keys[1]) --ed_new;
  if (w == 2) {
    if (keys[1] != key_in) ++ed_new;
  } else {
    const uint8_t old_tail = keys[w - 1];
    if (keys[w - 2] != old_tail) --ed_new;
    if (keys[w - 2] != key_in) ++ed_new;
    if (key_in != keys[0]) ++ed_new;
  }
  return static_cast<int32_t>(ed_new) - static_cast<int32_t>(ed_old);
}

inline bool ProbeHomAt(uint32_t* h, uint32_t mask, uint32_t w,
                       const uint32_t gear[256], const uint8_t* data, size_t pos,
                       size_t* out_cut) {
  const uint32_t g = gear[data[pos]];
  if ((*h & mask) == 0) {
    uint8_t keys[kTopoMaxWindow]{};
    uint32_t ed = 0;
    uint8_t prev = TopoKey(gear, data[pos - w]);
    keys[0] = prev;
    for (uint32_t i = 1; i < w; ++i) {
      const uint8_t k = TopoKey(gear, data[pos - w + i]);
      keys[i] = k;
      ed += prev != k;
      prev = k;
    }
    if (PrimaryTopoDelta(keys, w, static_cast<uint8_t>(g & 0xFFu), ed) != 0) {
      *out_cut = pos;
      return true;
    }
  }
  *h = (*h << 1) + g - gear[data[pos - w]];
  return false;
}

bool RunHomScanLoop(const uint8_t* data, size_t p_start, size_t cut_limit,
                    uint32_t w, uint32_t mask, const uint32_t gear[256],
                    uint32_t h, size_t* out_cut, uint64_t* probes) {
  size_t p = p_start;
#if defined(__AVX2__) || defined(_M_AVX2)
  for (; p + 8 <= cut_limit; p += 8) {
    _mm_prefetch(reinterpret_cast<const char*>(data + p + 64), _MM_HINT_T0);
    for (size_t j = 0; j < 8; ++j) {
      const size_t pos = p + j;
      if (probes) ++*probes;
      if (ProbeHomAt(&h, mask, w, gear, data, pos, out_cut)) {
        return true;
      }
    }
  }
#endif
  for (; p < cut_limit; ++p) {
    if (probes) ++*probes;
    if (ProbeHomAt(&h, mask, w, gear, data, p, out_cut)) {
      return true;
    }
  }
  return false;
}

}  // namespace

void SlotUfWindow::Reset(uint32_t window_w) {
  w = std::min(window_w, static_cast<uint32_t>(kTopoMaxWindow));
  wmask = (w > 0 && (w & (w - 1)) == 0) ? (w - 1) : 0;
  head = 0;
  filled = 0;
  components = 0;
  edge_diff = 0;
  std::memset(key, 0, sizeof(key));
  std::memset(parent, 0, sizeof(parent));
  std::memset(rank, 0, sizeof(rank));
}

void SlotUfWindow::SyncEdgeDiffFromKeys() {
  edge_diff = 0;
  if (filled < 2) return;
  for (uint32_t i = 0; i + 1 < filled; ++i) {
    const uint32_t s0 = (head + i) % w;
    const uint32_t s1 = (head + i + 1) % w;
    edge_diff += EdgeDiff(key, s0, s1);
  }
}

void SlotUfWindow::RebuildComponents() {
  for (uint32_t i = 0; i < w; ++i) parent[i] = static_cast<uint8_t>(i);
  std::memset(rank, 0, sizeof(rank));
  if (filled == 0) {
    components = 0;
    edge_diff = 0;
    return;
  }
  components = filled;
  for (uint32_t i = 0; i + 1 < filled; ++i) {
    const uint32_t s0 = (head + i) % w;
    const uint32_t s1 = (head + i + 1) % w;
    if (key[s0] == key[s1]) {
      if (UnionSlots(parent, rank, static_cast<uint8_t>(s0),
                     static_cast<uint8_t>(s1))) {
        --components;
      }
    }
  }
  SyncEdgeDiffFromKeys();
}

void SlotUfWindow::LoadWindow(const uint8_t* keys, size_t count) {
  filled = static_cast<uint32_t>(std::min(count, static_cast<size_t>(w)));
  head = 0;
  for (uint32_t i = 0; i < filled; ++i) {
    key[i] = keys[i];
  }
  RebuildComponents();
}

void SlotUfWindow::RecountEdgeDiff() {
  SyncEdgeDiffFromKeys();
}

int32_t SlotUfWindow::SlideViaRebuild(uint8_t key_in) {
  if (filled < w) {
    key[filled] = key_in;
    ++filled;
    RebuildComponents();
    return static_cast<int32_t>(components);
  }
  const uint32_t c_before = components;
  const uint32_t tail = (head + w - 1) % w;
  head = (head + 1) % w;
  key[tail] = key_in;
  RebuildComponents();
  return static_cast<int32_t>(components) - static_cast<int32_t>(c_before);
}

int32_t SlotUfWindow::Slide(uint8_t key_in) {
  if (filled < w) {
    key[filled] = key_in;
    ++filled;
    RebuildComponents();
    return static_cast<int32_t>(components);
  }
  if (w <= 1) {
    key[head] = key_in;
    return 0;
  }

  const uint32_t c_before = components;
  const uint32_t head_old = head;
  const uint32_t tail = SlotIndex(wmask, w, head_old + w - 1);
  const uint8_t old_tail = key[tail];
  const uint8_t head_old_key = key[head_old];
  edge_diff = ApplySlideEdgeDiffO1(*this, key_in, head_old, old_tail);
  head = SlotIndex(wmask, w, head_old + 1);
  key[tail] = key_in;
  components = ComponentsAfterSlide(edge_diff, key_in, head_old_key, w);

#ifndef NDEBUG
  const uint32_t ed_o1 = edge_diff;
  const uint32_t c_o1 = components;
  RecountEdgeDiff();
  assert(edge_diff == ed_o1);
  assert(c_o1 == ComponentsAfterSlide(edge_diff, key_in, head_old_key, w));
#endif

  return static_cast<int32_t>(components) - static_cast<int32_t>(c_before);
}

void InitGearTable(uint32_t gear[256], uint32_t seed) {
  gtcdc_internal::InitKeyedGearTable(gear, seed);
}

void BuildTopoMasks(uint32_t avg_size, uint16_t calib_permille, uint8_t topo_shift,
                    uint32_t* mask_out) {
  if (!mask_out) return;
  if (calib_permille > 0) {
    const double p_topo = std::max(static_cast<double>(calib_permille) / 1000.0, 0.001);
    const uint32_t effective =
        std::max<uint32_t>(static_cast<uint32_t>(static_cast<double>(avg_size) * p_topo),
                           1u);
    *mask_out = gtcdc_internal::BuildMask(effective);
    return;
  }
  const uint32_t shifted = std::max<uint32_t>(avg_size >> topo_shift, 1u);
  *mask_out = gtcdc_internal::BuildMask(shifted);
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

uint16_t CalibrateTopoPermille(const uint8_t* sample, size_t sample_len,
                               const TopoCdcConfig& cfg, uint32_t seed) {
  if (!sample || sample_len < cfg.min_size + cfg.window_w + 1024) return 500;

  uint32_t gear[256]{};
  InitGearTable(gear, seed);

  uint32_t mask = 0;
  BuildTopoMasks(cfg.avg_size, 0, cfg.topo_shift, &mask);

  size_t pos = 0;
  uint64_t probes = 0;
  uint64_t topo_hits = 0;

  while (pos + cfg.min_size + cfg.window_w < sample_len && pos < sample_len / 2) {
    const size_t scan_start = pos + cfg.min_size;
    const size_t cut_limit =
        std::min(pos + cfg.max_size, sample_len);
    if (scan_start >= cut_limit || scan_start < cfg.window_w) break;

    SlotUfWindow uf;
    uf.Reset(cfg.window_w);
    uint8_t keys[kTopoMaxWindow]{};
    for (uint32_t i = 0; i < cfg.window_w; ++i) {
      keys[i] = TopoKey(gear, sample[scan_start - cfg.window_w + i]);
    }
    uf.LoadWindow(keys, cfg.window_w);

    for (size_t p = scan_start; p < cut_limit && p < scan_start + 65536; ++p) {
      ++probes;
      const int32_t delta = uf.Slide(TopoKey(gear, sample[p]));
      if (delta != 0) ++topo_hits;
    }
    pos = cut_limit;
  }

  if (probes == 0) return 500;
  const double p_topo =
      std::max(static_cast<double>(topo_hits) / static_cast<double>(probes), 0.05);
  uint16_t permille =
      std::max<uint16_t>(static_cast<uint16_t>(std::min(999.0, p_topo * 1000.0 + 0.5)), 1u);

  if (sample_len < cfg.avg_size * 4) {
    return permille;
  }

  const double lo_target = static_cast<double>(cfg.avg_size) * 0.85;
  const double hi_target = static_cast<double>(cfg.avg_size) * 1.15;
  uint16_t lo = 1;
  uint16_t hi = 128;
  for (int iter = 0; iter < 16; ++iter) {
    const uint16_t mid = static_cast<uint16_t>((lo + hi) / 2);
    const double mean = MeanChunkLenHom(sample, sample_len, cfg, mid, gear);
    if (mean >= lo_target && mean <= hi_target) {
      return mid;
    }
    if (mean > hi_target) {
      if (mid <= 1) break;
      hi = mid - 1;
    } else {
      if (mid >= hi) break;
      lo = static_cast<uint16_t>(mid + 1);
    }
  }

  uint16_t best = permille;
  double best_err = 1e300;
  for (uint16_t p = lo; p <= hi; ++p) {
    const double mean = MeanChunkLenHom(sample, sample_len, cfg, p, gear);
    if (mean >= lo_target && mean <= hi_target) {
      return p;
    }
    const double err = std::abs(mean - static_cast<double>(cfg.avg_size));
    if (err < best_err) {
      best_err = err;
      best = p;
    }
  }
  return best;
}

void FillTopoCalibSample(uint8_t* out, size_t len, uint32_t seed) {
  if (!out || len == 0) return;
  uint64_t state = seed ? seed : 0xA5B4C3D2u;
  for (size_t i = 0; i < len; ++i) {
    state += 0x9e3779b97f4a7c15ULL;
    state = (state ^ (state >> 30)) * 0xbf58476d1ce4e5b9ULL;
    state = (state ^ (state >> 27)) * 0x94d049bb133111ebULL;
    state = state ^ (state >> 31);
    out[i] = static_cast<uint8_t>(state);
  }
}

bool ScanHomCut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                uint32_t w, uint32_t mask, const uint32_t gear[256],
                size_t* out_cut, bool* found, uint64_t* probes) {
  if (!out_cut || !found) return false;
  *found = false;
  if (scan_start >= cut_limit || scan_start < w) return false;

  uint32_t h = InitWindowHash(data, scan_start, w, gear);
  size_t cut = scan_start;
  if (RunHomScanLoop(data, scan_start, cut_limit, w, mask, gear, h, &cut,
                     probes)) {
    *out_cut = cut;
    *found = true;
    return true;
  }
  return false;
}

namespace {

double MeanChunkLenHom(const uint8_t* data, size_t len, const TopoCdcConfig& cfg,
                       uint16_t calib_permille, const uint32_t gear[256]) {
  if (len == 0) return 0.0;
  uint32_t mask = 0;
  BuildTopoMasks(cfg.avg_size, calib_permille, cfg.topo_shift, &mask);
  size_t pos = 0;
  double sum = 0.0;
  size_t count = 0;
  while (pos < len) {
    const size_t remaining = len - pos;
    if (remaining <= cfg.min_size) {
      sum += static_cast<double>(remaining);
      ++count;
      break;
    }
    const size_t rel_scan = cfg.min_size;
    const size_t rel_limit = std::min<size_t>(cfg.max_size, remaining);
    size_t rel_cut = rel_limit;
    bool found = false;
    if (rel_scan < rel_limit) {
      ScanHomCut(data + pos, rel_scan, rel_limit, cfg.window_w, mask, gear,
                 &rel_cut, &found);
    }
    size_t cut = 0;
    if (found) {
      cut = pos + rel_cut;
    } else if (remaining > cfg.max_size) {
      cut = pos + cfg.max_size;
    } else {
      cut = len;
    }
    sum += static_cast<double>(cut - pos);
    ++count;
    pos = cut;
  }
  return count ? sum / static_cast<double>(count) : 0.0;
}

}  // namespace

void ClearTopoHomResume(TopoHomResume* resume) {
  if (!resume) return;
  resume->h = 0;
  resume->scan_rel = 0;
  resume->in_scan = false;
  resume->window_w = 64;
}

bool ProcessHomChunk(const uint8_t* data, size_t len, size_t chunk_pos,
                     bool allow_tail_cut, const TopoCdcConfig& cfg,
                     uint32_t mask, const uint32_t gear[256],
                     TopoHomResume* resume, size_t* out_cut, bool* chunk_done,
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
      ClearTopoHomResume(resume);
    }
    return true;
  }
  if (scan_start < w) {
    if (allow_tail_cut) {
      *out_cut = cut_limit;
      *chunk_done = true;
      ClearTopoHomResume(resume);
    }
    return true;
  }

  uint32_t h = 0;
  size_t p = scan_start;
  if (resume->in_scan && resume->scan_rel >= scan_start &&
      resume->scan_rel < cut_limit) {
    h = resume->h;
    p = resume->scan_rel;
  } else {
    resume->window_w = w;
    h = InitWindowHash(data, scan_start, w, gear);
    resume->in_scan = true;
    p = scan_start;
  }

  size_t cut = p;
  if (RunHomScanLoop(data, p, cut_limit, w, mask, gear, h, &cut, probes)) {
    *out_cut = cut;
    *chunk_done = true;
    ClearTopoHomResume(resume);
    return true;
  }

  resume->h = h;
  resume->scan_rel = cut_limit;
  resume->in_scan = true;

  const size_t remaining = len - chunk_pos;
  if (remaining > cfg.max_size) {
    *out_cut = cut_limit;
    *chunk_done = true;
    ClearTopoHomResume(resume);
    return true;
  }

  if (allow_tail_cut && len > chunk_pos) {
    *out_cut = len;
    *chunk_done = true;
    ClearTopoHomResume(resume);
    return true;
  }
  return true;
}

}  // namespace topo_cdc_internal
}  // namespace ebbackup
