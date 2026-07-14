#include "ebbackup/chunk/topo_chain_internal.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <future>
#include <vector>

#include "ebbackup/chunk/topo_cdc_internal.h"

#if defined(__AVX2__) || defined(_M_AVX2)
#include <immintrin.h>
#endif

namespace ebbackup {
namespace topo_chain_internal {

namespace {

uint8_t ChainKey(const uint8_t* data, size_t pos, uint8_t quant_q) {
  return ChainQuantKey(data[pos], quant_q);
}

// Stride-hit path: O(w) rebuild (rare when stride_log≈12). Non-stride: O(1) LFSR only.
inline bool ProbeChainAt(uint32_t* s, uint32_t stride_mask, uint32_t w,
                         uint8_t quant_q, bool enable_beta1, const uint8_t* data,
                         size_t pos, size_t* out_cut) {
  if ((*s & stride_mask) == 0) {
    uint8_t keys[kChainMaxWindow]{};
    uint32_t ed = 0;
    uint8_t prev = ChainKey(data, pos - w, quant_q);
    keys[0] = prev;
    for (uint32_t i = 1; i < w; ++i) {
      const uint8_t k = ChainKey(data, pos - w + i, quant_q);
      keys[i] = k;
      ed += prev != k;
      prev = k;
    }
    const uint8_t key_in = ChainKey(data, pos, quant_q);
    if (ChainPrimaryDelta(keys, w, key_in, ed) != 0) {
      uint8_t slide[kChainMaxWindow]{};
      for (uint32_t i = 0; i + 1 < w; ++i) slide[i] = keys[i + 1];
      slide[w - 1] = key_in;
      if (!enable_beta1 || ChainBeta1Gf2(slide, w) > 0) {
        *out_cut = pos;
        return true;
      }
    }
  }
  *s = UpdateChainSignature(*s, data[pos], quant_q);
  return false;
}

bool RunChainScanLoop(const uint8_t* data, size_t p_start, size_t cut_limit,
                      uint32_t w, uint32_t stride_mask, uint8_t quant_q,
                      bool enable_beta1, uint32_t* s, size_t* out_cut,
                      uint64_t* probes, std::atomic<bool>* cancel) {
  size_t p = p_start;
  for (; p + 8 <= cut_limit; p += 8) {
    if (cancel && cancel->load(std::memory_order_relaxed)) return false;
#if defined(__AVX2__) || defined(_M_AVX2)
    _mm_prefetch(reinterpret_cast<const char*>(data + p + 64), _MM_HINT_T0);
#endif
    for (size_t j = 0; j < 8; ++j) {
      const size_t pos = p + j;
      if (probes) ++*probes;
      if (ProbeChainAt(s, stride_mask, w, quant_q, enable_beta1, data, pos,
                       out_cut)) {
        return true;
      }
    }
  }
  for (; p < cut_limit; ++p) {
    if (cancel && cancel->load(std::memory_order_relaxed)) return false;
    if (probes) ++*probes;
    if (ProbeChainAt(s, stride_mask, w, quant_q, enable_beta1, data, p,
                     out_cut)) {
      return true;
    }
  }
  return false;
}

double MeanChunkLenChain(const uint8_t* data, size_t len, const TopoCdcConfig& cfg,
                         uint8_t stride_log, uint8_t quant_q) {
  if (len == 0) return 0.0;
  TopoCdcConfig run = cfg;
  run.chain_stride_log = stride_log;
  run.chain_quant_q = quant_q;
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
      ScanChainCut(data + pos, rel_scan, rel_limit, run, &rel_cut, &found);
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

constexpr size_t kParallelSegmentBytes = 256 * 1024;

bool ChainParallelScanEnabled() {
  const char* env = std::getenv("EBBACKUP_TOPOCHAIN_PARALLEL_SCAN");
  return env && env[0] == '1';
}

bool RunChainScanLoopParallel(const uint8_t* data, size_t p_start,
                              size_t cut_limit, uint32_t w,
                              uint32_t stride_mask, uint8_t quant_q,
                              bool enable_beta1, uint32_t lfsr_seed,
                              size_t* out_cut, uint64_t* probes) {
  const size_t span = cut_limit - p_start;
  if (span <= kParallelSegmentBytes) {
    uint32_t s = InitChainSignature(data, p_start, w, quant_q, lfsr_seed);
    return RunChainScanLoop(data, p_start, cut_limit, w, stride_mask, quant_q,
                            enable_beta1, &s, out_cut, probes, nullptr);
  }

  const size_t num_segs =
      (span + kParallelSegmentBytes - 1) / kParallelSegmentBytes;
  std::vector<uint32_t> s_at_seg(num_segs);
  {
    uint32_t s = InitChainSignature(data, p_start, w, quant_q, lfsr_seed);
    s_at_seg[0] = s;
    for (size_t i = 0; i + 1 < num_segs; ++i) {
      const size_t seg_begin = p_start + i * kParallelSegmentBytes;
      const size_t seg_end =
          std::min(p_start + (i + 1) * kParallelSegmentBytes, cut_limit);
      for (size_t j = seg_begin; j < seg_end; ++j) {
        s = UpdateChainSignature(s, data[j], quant_q);
      }
      s_at_seg[i + 1] = s;
    }
  }

  std::vector<size_t> cuts(num_segs, cut_limit);
  std::vector<char> found(num_segs, 0);
  std::vector<uint64_t> local_probes(num_segs, 0);
  std::atomic<bool> cancel{false};
  std::vector<std::future<void>> futures;
  futures.reserve(num_segs);

  for (size_t i = 0; i < num_segs; ++i) {
    const size_t seg_begin = p_start + i * kParallelSegmentBytes;
    if (seg_begin >= cut_limit) break;
    const size_t seg_end =
        std::min(p_start + (i + 1) * kParallelSegmentBytes, cut_limit);
    futures.push_back(std::async(std::launch::async, [&, i, seg_begin,
                                                       seg_end]() {
      if (cancel.load(std::memory_order_relaxed)) return;
      uint32_t s = s_at_seg[i];
      size_t cut = seg_begin;
      if (RunChainScanLoop(data, seg_begin, seg_end, w, stride_mask, quant_q,
                           enable_beta1, &s, &cut,
                           probes ? &local_probes[i] : nullptr, &cancel)) {
        found[i] = 1;
        cuts[i] = cut;
      }
    }));
  }

  size_t best = cut_limit;
  bool any = false;
  for (size_t i = 0; i < futures.size(); ++i) {
    futures[i].wait();
    if (probes) *probes += local_probes[i];
    if (found[i] && cuts[i] < best) {
      best = cuts[i];
      any = true;
      cancel.store(true, std::memory_order_relaxed);
      for (size_t j = i + 1; j < futures.size(); ++j) {
        futures[j].wait();
        if (probes) *probes += local_probes[j];
      }
      break;
    }
  }
  if (any) {
    *out_cut = best;
    return true;
  }
  return false;
}

bool RunChainScanLoopDispatch(const uint8_t* data, size_t p_start,
                              size_t cut_limit, uint32_t w,
                              uint32_t stride_mask, uint8_t quant_q,
                              bool enable_beta1, uint32_t lfsr_seed,
                              uint32_t* s, uint8_t* /*keys*/,
                              uint32_t* /*edge_diff*/, bool force_serial,
                              size_t* out_cut, uint64_t* probes) {
  if (!force_serial && ChainParallelScanEnabled()) {
    return RunChainScanLoopParallel(data, p_start, cut_limit, w, stride_mask,
                                    quant_q, enable_beta1, lfsr_seed, out_cut,
                                    probes);
  }
  return RunChainScanLoop(data, p_start, cut_limit, w, stride_mask, quant_q,
                          enable_beta1, s, out_cut, probes, nullptr);
}

uint32_t Rotl32(uint32_t v, uint32_t bits) {
  bits &= 31u;
  if (bits == 0) return v;
  return (v << bits) | (v >> (32u - bits));
}

uint8_t ChainQuantKey(uint8_t byte, uint8_t quant_q) {
  if (quant_q >= 8) return 0;
  return static_cast<uint8_t>(byte >> quant_q);
}

uint32_t UpdateChainSignature(uint32_t s, uint8_t byte, uint8_t quant_q) {
  return Rotl32(s, 1) ^ static_cast<uint32_t>(ChainQuantKey(byte, quant_q));
}

uint32_t InitChainSignature(const uint8_t* data, size_t end, uint32_t w,
                            uint8_t quant_q, uint32_t lfsr_seed) {
  uint32_t s = lfsr_seed;
  if (end < w) return s;
  const size_t start = end - w;
  for (size_t i = start; i < end; ++i) {
    s = UpdateChainSignature(s, data[i], quant_q);
  }
  return s;
}

uint32_t ChainSignatureAt(const uint8_t* data, size_t scan_start, size_t pos,
                          uint32_t w, uint8_t quant_q, uint32_t lfsr_seed) {
  uint32_t s = InitChainSignature(data, scan_start, w, quant_q, lfsr_seed);
  for (size_t i = scan_start; i < pos; ++i) {
    s = UpdateChainSignature(s, data[i], quant_q);
  }
  return s;
}

uint32_t ChainStrideMask(uint8_t stride_log) {
  if (stride_log == 0) return 0u;
  if (stride_log >= 31) return 0x7FFFFFFFu;
  return (1u << stride_log) - 1u;
}

int32_t ChainPrimaryDelta(const uint8_t* keys, uint32_t w, uint8_t key_in,
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

uint32_t ChainBeta1Gf2Reference(const uint8_t* keys, uint32_t w) {
  if (!keys || w < 3) return 0;

  uint8_t parent[256];
  uint8_t present[256]{};
  for (uint32_t i = 0; i < 256; ++i) parent[i] = static_cast<uint8_t>(i);

  auto find = [&](uint8_t x) -> uint8_t {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  };
  auto unite = [&](uint8_t a, uint8_t b) {
    a = find(a);
    b = find(b);
    if (a != b) parent[b] = a;
  };

  uint16_t uniq_edges[128]{};
  uint32_t e_count = 0;
  for (uint32_t i = 0; i + 1 < w && e_count < 128; ++i) {
    uint8_t a = keys[i];
    uint8_t b = keys[i + 1];
    if (a == b) continue;
    present[a] = 1;
    present[b] = 1;
    uint8_t lo = a < b ? a : b;
    uint8_t hi = a < b ? b : a;
    const uint16_t edge =
        static_cast<uint16_t>((static_cast<uint16_t>(lo) << 8) | hi);
    bool seen = false;
    for (uint32_t j = 0; j < e_count; ++j) {
      if (uniq_edges[j] == edge) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      uniq_edges[e_count++] = edge;
      unite(a, b);
    }
  }

  uint32_t v_count = 0;
  uint32_t c_count = 0;
  for (uint32_t i = 0; i < 256; ++i) {
    if (!present[i]) continue;
    ++v_count;
    if (find(static_cast<uint8_t>(i)) == i) ++c_count;
  }
  if (v_count == 0) return 0;
  if (e_count + c_count <= v_count) return 0;
  return e_count + c_count - v_count;
}

uint32_t ChainBeta1Gf2(const uint8_t* keys, uint32_t w) {
  if (!keys || w < 3) return 0;

  uint8_t parent[256];
  uint8_t touched[kChainMaxWindow]{};
  uint32_t touched_n = 0;
  uint64_t edge_bits[1024]{};

  auto edge_seen = [&](uint8_t lo, uint8_t hi) -> bool {
    const uint32_t idx =
        (static_cast<uint32_t>(lo) << 8) | static_cast<uint32_t>(hi);
    return (edge_bits[idx >> 6] >> (idx & 63)) & 1ull;
  };
  auto edge_set = [&](uint8_t lo, uint8_t hi) {
    const uint32_t idx =
        (static_cast<uint32_t>(lo) << 8) | static_cast<uint32_t>(hi);
    edge_bits[idx >> 6] |= 1ull << (idx & 63);
  };

  for (uint32_t i = 0; i < 256; ++i) parent[i] = static_cast<uint8_t>(i);

  auto find = [&](uint8_t x) -> uint8_t {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  };
  auto unite = [&](uint8_t a, uint8_t b) {
    a = find(a);
    b = find(b);
    if (a != b) parent[b] = a;
  };

  uint8_t present[256]{};
  uint32_t e_count = 0;
  for (uint32_t i = 0; i + 1 < w; ++i) {
    uint8_t a = keys[i];
    uint8_t b = keys[i + 1];
    if (a == b) continue;
    if (!present[a]) {
      present[a] = 1;
      touched[touched_n++] = a;
    }
    if (!present[b]) {
      present[b] = 1;
      touched[touched_n++] = b;
    }
    uint8_t lo = a < b ? a : b;
    uint8_t hi = a < b ? b : a;
    if (edge_seen(lo, hi)) continue;
    edge_set(lo, hi);
    ++e_count;
    unite(a, b);
  }

  if (touched_n == 0) return 0;

  uint32_t c_count = 0;
  for (uint32_t i = 0; i < touched_n; ++i) {
    const uint8_t v = touched[i];
    if (find(v) == v) ++c_count;
  }
  const uint32_t v_count = touched_n;
  if (e_count + c_count <= v_count) return 0;
  return e_count + c_count - v_count;
}

uint32_t SyncEdgeDiffFromKeys(const uint8_t* keys, uint32_t w) {
  uint32_t ed = 0;
  for (uint32_t i = 0; i + 1 < w; ++i) {
    ed += keys[i] != keys[i + 1];
  }
  return ed;
}

void FillChainCalibSample(uint8_t* out, size_t len, uint32_t seed) {
  topo_cdc_internal::FillTopoCalibSample(out, len, seed);
}

uint16_t CalibrateChainStrideLog(const uint8_t* sample, size_t sample_len,
                                 const TopoCdcConfig& cfg) {
  if (!sample || sample_len < cfg.min_size + cfg.window_w + 1024) return 12;

  const double lo_target = static_cast<double>(cfg.avg_size) * 0.85;
  const double hi_target = static_cast<double>(cfg.avg_size) * 1.15;

  uint16_t lo = 8;
  uint16_t hi = 22;
  uint16_t best = 12;
  double best_err = 1e300;
  while (lo <= hi) {
    const uint16_t mid = static_cast<uint16_t>((lo + hi) / 2);
    const double mean =
        MeanChunkLenChain(sample, sample_len, cfg, static_cast<uint8_t>(mid),
                          cfg.chain_quant_q);
    const double err = std::abs(mean - static_cast<double>(cfg.avg_size));
    if (err < best_err) {
      best_err = err;
      best = mid;
    }
    if (mean >= lo_target && mean <= hi_target) {
      uint16_t pick = mid;
      double pick_err = err;
      for (int d = -1; d <= 1; ++d) {
        const int cand = static_cast<int>(mid) + d;
        if (cand < 8 || cand > 22) continue;
        const double m =
            MeanChunkLenChain(sample, sample_len, cfg,
                              static_cast<uint8_t>(cand), cfg.chain_quant_q);
        if (m < lo_target || m > hi_target) continue;
        const double e = std::abs(m - static_cast<double>(cfg.avg_size));
        if (e < pick_err) {
          pick_err = e;
          pick = static_cast<uint16_t>(cand);
        }
      }
      return pick;
    }
    if (mean < lo_target) {
      lo = static_cast<uint16_t>(mid + 1);
    } else {
      if (mid == 0) break;
      hi = static_cast<uint16_t>(mid - 1);
    }
  }
  return best;
}

bool ScanChainCut(const uint8_t* data, size_t scan_start, size_t cut_limit,
                  const TopoCdcConfig& cfg, size_t* out_cut, bool* found,
                  uint64_t* probes) {
  if (!out_cut || !found) return false;
  *found = false;
  if (scan_start >= cut_limit || scan_start < cfg.window_w) return false;

  const uint32_t w = cfg.window_w;
  const uint32_t stride_mask = ChainStrideMask(cfg.chain_stride_log);
  uint32_t s = InitChainSignature(data, scan_start, w, cfg.chain_quant_q,
                                  cfg.chain_lfsr_seed);
  uint8_t keys_dummy[1]{};
  uint32_t ed_dummy = 0;
  size_t cut = scan_start;
  if (RunChainScanLoopDispatch(data, scan_start, cut_limit, w, stride_mask,
                               cfg.chain_quant_q, cfg.chain_enable_beta1,
                               cfg.chain_lfsr_seed, &s, keys_dummy, &ed_dummy,
                               false, &cut, probes)) {
    *out_cut = cut;
    *found = true;
    return true;
  }
  return false;
}

void ClearTopoChainResume(TopoChainResume* resume) {
  if (!resume) return;
  resume->S = 0;
  resume->scan_rel = 0;
  resume->in_scan = false;
  resume->window_ready = false;
  resume->window_w = 64;
  resume->edge_diff = 0;
}

bool ProcessChainChunk(const uint8_t* data, size_t len, size_t chunk_pos,
                       bool allow_tail_cut, const TopoCdcConfig& cfg,
                       TopoChainResume* resume, size_t* out_cut,
                       bool* chunk_done, uint64_t* probes) {
  if (!resume || !out_cut || !chunk_done) return false;
  *chunk_done = false;

  const uint32_t w = cfg.window_w;
  const size_t scan_start = chunk_pos + cfg.min_size;
  const size_t cut_limit = std::min(chunk_pos + cfg.max_size, len);
  if (scan_start >= cut_limit) {
    if (allow_tail_cut && cut_limit > chunk_pos) {
      *out_cut = cut_limit;
      *chunk_done = true;
      ClearTopoChainResume(resume);
    }
    return true;
  }
  if (scan_start < w) {
    if (allow_tail_cut) {
      *out_cut = cut_limit;
      *chunk_done = true;
      ClearTopoChainResume(resume);
    }
    return true;
  }

  const uint32_t stride_mask = ChainStrideMask(cfg.chain_stride_log);
  size_t p = scan_start;
  uint32_t s = 0;

  if (resume->in_scan && resume->window_ready && resume->window_w == w &&
      resume->scan_rel >= scan_start && resume->scan_rel < cut_limit) {
    p = resume->scan_rel;
    s = resume->S;
  } else {
    resume->window_w = w;
    resume->in_scan = true;
    p = scan_start;
    s = InitChainSignature(data, scan_start, w, cfg.chain_quant_q,
                           cfg.chain_lfsr_seed);
  }

  const bool force_serial =
      (resume->in_scan && resume->scan_rel > scan_start) || p > scan_start;

  uint8_t keys_dummy[1]{};
  uint32_t ed_dummy = 0;
  size_t cut = p;
  if (RunChainScanLoopDispatch(data, p, cut_limit, w, stride_mask,
                               cfg.chain_quant_q, cfg.chain_enable_beta1,
                               cfg.chain_lfsr_seed, &s, keys_dummy, &ed_dummy,
                               force_serial, &cut, probes)) {
    *out_cut = cut;
    *chunk_done = true;
    ClearTopoChainResume(resume);
    return true;
  }

  resume->S = s;
  resume->scan_rel = cut_limit;
  resume->in_scan = true;
  resume->window_ready = true;
  resume->window_w = w;

  const size_t remaining = len - chunk_pos;
  if (remaining > cfg.max_size) {
    *out_cut = cut_limit;
    *chunk_done = true;
    ClearTopoChainResume(resume);
    return true;
  }

  if (allow_tail_cut && len > chunk_pos) {
    *out_cut = len;
    *chunk_done = true;
    ClearTopoChainResume(resume);
    return true;
  }
  return true;
}

}  // namespace topo_chain_internal
}  // namespace ebbackup
