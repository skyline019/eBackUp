#include "ebbackup/chunk/gt_cdc_streaming.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <vector>

#include "ebbackup/chunk/gt_cdc_internal.h"
#include "ebbackup/chunk/gt_cdc_view.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/digest_pool.h"

namespace ebbackup {

namespace {

constexpr size_t kParallelHashMinBytes = 1024 * 1024;
constexpr size_t kParallelHashMinChunks = 2;

class ScopedStreamTimer {
 public:
  explicit ScopedStreamTimer(uint64_t* field) : field_(field), enabled_(field != nullptr) {
    if (enabled_) t0_ = std::chrono::steady_clock::now();
  }
  ~ScopedStreamTimer() {
    if (!enabled_ || !field_) return;
    const auto t1 = std::chrono::steady_clock::now();
    *field_ += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0_).count());
  }

 private:
  uint64_t* field_;
  bool enabled_;
  std::chrono::steady_clock::time_point t0_{};
};

bool StreamProfileEnabled() {
  const char* env = std::getenv("EBBACKUP_PIPELINE_PROFILE");
  return env && env[0] == '1';
}

uint32_t FeedViewRangeCompose(const GtCdcSegmentView& view, size_t start, size_t len,
                              uint32_t h, const GtCdcConfig& cfg,
                              const uint32_t pow[gtcdc_internal::kAlphaPowTableSize],
                              GtCdcStreamProfile* profile) {
  if (len == 0) return h;
  const size_t end = start + len;

  if (end <= view.len0) {
    h = gtcdc_internal::FeedRangeCompose(view.seg0 + start, len, h, cfg.alpha,
                                         cfg.beta_table, cfg.block_B, pow);
    if (profile) {
      profile->blocks_composed +=
          (len + cfg.block_B - 1) / std::max(cfg.block_B, 1u);
    }
    return h;
  }
  if (start >= view.len0) {
    h = gtcdc_internal::FeedRangeCompose(view.seg1 + (start - view.len0), len, h,
                                         cfg.alpha, cfg.beta_table, cfg.block_B,
                                         pow);
    if (profile) {
      profile->blocks_composed +=
          (len + cfg.block_B - 1) / std::max(cfg.block_B, 1u);
    }
    return h;
  }

  const size_t part0 = view.len0 - start;
  h = gtcdc_internal::FeedRangeCompose(view.seg0 + start, part0, h, cfg.alpha,
                                       cfg.beta_table, cfg.block_B, pow);
  if (profile) {
    profile->blocks_composed +=
        (part0 + cfg.block_B - 1) / std::max(cfg.block_B, 1u);
  }
  h = gtcdc_internal::FeedRangeCompose(view.seg1, len - part0, h, cfg.alpha,
                                       cfg.beta_table, cfg.block_B, pow);
  if (profile) {
    profile->blocks_composed +=
        ((len - part0) + cfg.block_B - 1) / std::max(cfg.block_B, 1u);
  }
  return h;
}

bool GtCdcScanView(const GtCdcSegmentView& view, size_t scan_start, size_t cut_limit,
                   uint32_t h_initial, const GtCdcConfig& cfg, uint32_t mask,
                   const uint32_t pow[gtcdc_internal::kAlphaPowTableSize],
                   size_t* out_cut, bool* found, uint32_t* h_out,
                   gtcdc_internal::GtCdcScanStats* stats) {
  if (!out_cut || !found) return false;
  *found = false;
  if (scan_start >= cut_limit) {
    if (h_out) *h_out = h_initial;
    return false;
  }

  if (view.len0 == 0) {
    return gtcdc_internal::GtCdcScan(view.seg1, scan_start, cut_limit, h_initial,
                                     cfg.alpha, cfg.beta_table, mask, cfg.block_B,
                                     pow, out_cut, found, h_out, stats);
  }

  if (scan_start >= view.len0 && cut_limit <= view.len0 + view.len1) {
    size_t rel_cut = cut_limit - view.len0;
    const bool ok = gtcdc_internal::GtCdcScan(
        view.seg1, scan_start - view.len0, rel_cut, h_initial, cfg.alpha,
        cfg.beta_table, mask, cfg.block_B, pow, &rel_cut, found, h_out, stats);
    if (*found) *out_cut = rel_cut + view.len0;
    return ok;
  }

  if (cut_limit <= view.len0) {
    return gtcdc_internal::GtCdcScan(view.seg0, scan_start, cut_limit, h_initial,
                                     cfg.alpha, cfg.beta_table, mask, cfg.block_B,
                                     pow, out_cut, found, h_out, stats);
  }

  if (scan_start >= view.len0) {
    return gtcdc_internal::GtCdcScan(view.seg1, scan_start - view.len0,
                                     cut_limit - view.len0, h_initial, cfg.alpha,
                                     cfg.beta_table, mask, cfg.block_B, pow,
                                     out_cut, found, h_out, stats);
  }

  const size_t seg0_limit = view.len0;
  size_t cut0 = seg0_limit;
  bool found0 = false;
  uint32_t h_mid = h_initial;
  gtcdc_internal::GtCdcScan(view.seg0, scan_start, seg0_limit, h_initial,
                            cfg.alpha, cfg.beta_table, mask, cfg.block_B, pow,
                            &cut0, &found0, &h_mid, stats);
  if (found0) {
    *found = true;
    *out_cut = cut0;
    if (h_out) *h_out = h_mid;
    return true;
  }

  size_t rel_cut = cut_limit - view.len0;
  const bool ok = gtcdc_internal::GtCdcScan(view.seg1, 0, rel_cut, h_mid, cfg.alpha,
                                            cfg.beta_table, mask, cfg.block_B, pow,
                                            &rel_cut, found, h_out, stats);
  if (*found) *out_cut = rel_cut + view.len0;
  return ok;
}

Status HashRegionsWithDigestBase(DigestAlgo algo, const uint8_t* digest_base,
                                 uint64_t logical_base,
                                 const std::vector<size_t>& offsets,
                                 const std::vector<uint32_t>& lengths,
                                 std::vector<ChunkDescriptor>* out) {
  out->clear();
  const size_t count = offsets.size();
  if (count == 0) return Status::Ok();
  if (digest_base == nullptr) {
    return Status::InvalidArgument(
        "G-TCDC streaming requires digest_base; no copy fallback");
  }

  const size_t total_bytes =
      std::accumulate(lengths.begin(), lengths.end(), size_t{0});
  out->resize(count);

  const bool use_pool = count >= kParallelHashMinChunks &&
                        total_bytes >= kParallelHashMinBytes;
  if (use_pool) {
    std::vector<DigestSpan> spans(count);
    for (size_t i = 0; i < count; ++i) {
      spans[i].offset = logical_base + offsets[i];
      spans[i].length = lengths[i];
    }
    std::vector<uint8_t> hashes(count * 32);
    DigestPool::Shared().HashRegions(algo, digest_base, spans.data(), count,
                                     hashes.data());
    for (size_t i = 0; i < count; ++i) {
      (*out)[i].offset = offsets[i];
      (*out)[i].length = lengths[i];
      std::memcpy((*out)[i].hash, hashes.data() + i * 32, 32);
    }
    return Status::Ok();
  }

  for (size_t i = 0; i < count; ++i) {
    (*out)[i].offset = offsets[i];
    (*out)[i].length = lengths[i];
    ContentHash(algo, digest_base + logical_base + offsets[i], lengths[i],
                (*out)[i].hash);
  }
  return Status::Ok();
}

void SetCarryTail(GtCdcStreamState* state, const GtCdcSegmentView& view,
                  size_t consumed, GtCdcStreamProfile* profile) {
  const size_t total = view.size();
  if (consumed >= total) {
    state->carry.clear();
    return;
  }
  const size_t tail_len = total - consumed;
  ScopedStreamTimer timer(profile ? &profile->carry_copy_ns : nullptr);
  state->carry.resize(tail_len);
  view.copy_range(consumed, tail_len, state->carry.data());
}

size_t AnGearChunkStartAbs(const GtCdcStreamState* state, size_t pos_bias,
                           size_t pos) {
  if (state->an_chunk_abs_start != ~0ULL) return state->an_chunk_abs_start;
  return pos_bias + pos;
}

size_t AnGearScanStart(const GtCdcStreamState* state, const GtCdcConfig& cfg,
                       size_t pos_bias, size_t pos) {
  if (state->an_scan_abs != ~0ULL && state->an_scan_abs > pos_bias + pos) {
    return state->an_scan_abs - pos_bias;
  }
  return pos + cfg.min_size;
}

void AnGearClearChunk(GtCdcStreamState* state) {
  state->an_chunk_abs_start = ~0ULL;
  state->an_scan_abs = ~0ULL;
  state->an_gear_h_valid = false;
}

void AnGearBeginChunk(GtCdcStreamState* state, size_t pos_bias, size_t pos,
                      const GtCdcConfig& cfg) {
  if (state->config.kernel != GtCdcKernel::kAnGear) return;
  if (state->an_chunk_abs_start == ~0ULL) {
    state->an_chunk_abs_start = pos_bias + pos;
    state->an_scan_abs = state->an_chunk_abs_start + cfg.min_size;
    state->an_gear_h_valid = false;
  }
}

void AnGearResumeScan(GtCdcStreamState* state, size_t pos_bias, size_t scan_end) {
  if (state->config.kernel != GtCdcKernel::kAnGear) return;
  state->an_scan_abs = pos_bias + scan_end;
}

bool ProcessAnGearChunkCut(GtCdcStreamState* state, const GtCdcSegmentView& view,
                           size_t proc_len, size_t pos, bool is_last,
                           const GtCdcConfig& cfg, size_t pos_bias,
                           uint64_t* probes, size_t* cut_out, bool* stop_outer) {
  *stop_outer = false;
  const size_t remaining = proc_len - pos;
  if (remaining <= cfg.min_size) {
    if (is_last) {
      *cut_out = proc_len;
      AnGearClearChunk(state);
      return true;
    }
    *stop_outer = true;
    return false;
  }

  AnGearBeginChunk(state, pos_bias, pos, cfg);
  const size_t chunk_start_abs = AnGearChunkStartAbs(state, pos_bias, pos);
  const size_t chunk_start_view = chunk_start_abs - pos_bias;
  const size_t scan_start = AnGearScanStart(state, cfg, pos_bias, pos);
  const uint32_t w = cfg.window_w;
  const uint32_t* gear = cfg.beta_table;
  size_t cut = std::min(chunk_start_view + cfg.max_size, proc_len);
  if (scan_start >= proc_len) {
    *stop_outer = true;
    return false;
  }
  const size_t chunk_buffered = pos_bias + proc_len - chunk_start_abs;
  if (scan_start >= cut) {
    if (chunk_buffered > cfg.max_size) {
      cut = chunk_start_abs + cfg.max_size - pos_bias;
      AnGearClearChunk(state);
      *cut_out = cut;
      return true;
    }
    if (is_last) {
      cut = proc_len;
      AnGearClearChunk(state);
      *cut_out = cut;
      return true;
    }
    AnGearResumeScan(state, pos_bias, proc_len);
    *stop_outer = true;
    return false;
  }
  bool found = false;
  uint32_t h_out = 0;
  const bool use_h_resume = state->an_gear_h_valid;

  if (scan_start >= w && scan_start < cut) {
    if (view.len0 == 0) {
      gtcdc_internal::ScanGearCutAn(
          view.seg1, scan_start, cut, chunk_start_abs, pos_bias, w,
          cfg.avg_size, cfg.nc_level, cfg.table_seed, gear, &cut, &found,
          state->an_gear_h, use_h_resume, &h_out, probes);
    } else if (scan_start >= view.len0 && cut <= view.len0 + view.len1) {
      size_t rel_cut = cut - view.len0;
      gtcdc_internal::ScanGearCutAn(
          view.seg1, scan_start - view.len0, rel_cut, chunk_start_abs,
          pos_bias + view.len0, w, cfg.avg_size, cfg.nc_level, cfg.table_seed,
          gear, &rel_cut, &found, state->an_gear_h, use_h_resume, &h_out,
          probes);
      if (found) cut = rel_cut + view.len0;
    } else {
      state->an_gear_h_valid = false;
      gtcdc_internal::GtCdcScanGearAnView(
          view, scan_start, cut, chunk_start_abs, pos_bias, w, cfg.avg_size,
          cfg.nc_level, cfg.table_seed, gear, &cut, &found);
    }
  }

  const size_t scan_end = cut;
  if (!found && chunk_buffered > cfg.max_size) {
    cut = chunk_start_abs + cfg.max_size - pos_bias;
    AnGearClearChunk(state);
  } else if (!found && is_last) {
    cut = proc_len;
    AnGearClearChunk(state);
  } else if (!found) {
    state->an_gear_h = h_out;
    state->an_gear_h_valid = true;
    AnGearResumeScan(state, pos_bias, scan_end);
    *stop_outer = true;
    return false;
  } else {
    AnGearClearChunk(state);
  }

  *cut_out = cut;
  return true;
}

void TwoFClearResume(GtCdcStreamState* state) {
  state->tf_scan_abs = ~0ULL;
  state->tf_h_valid = false;
}

bool Process2FGearChunkCut(GtCdcStreamState* state, const GtCdcSegmentView& view,
                           size_t proc_len, size_t pos, bool is_last,
                           const GtCdcConfig& cfg, size_t pos_bias,
                           uint64_t* probes, size_t* cut_out, bool* stop_outer) {
  *stop_outer = false;
  const size_t remaining = proc_len - pos;
  if (remaining <= cfg.min_size) {
    if (is_last) {
      *cut_out = proc_len;
      TwoFClearResume(state);
      return true;
    }
    *stop_outer = true;
    return false;
  }

  const uint32_t w = cfg.window_w;
  size_t scan_start = pos + cfg.min_size;
  if (state->tf_scan_abs != ~0ULL && state->tf_scan_abs > pos_bias + pos) {
    scan_start = state->tf_scan_abs - pos_bias;
  }
  size_t cut = std::min(pos + cfg.max_size, proc_len);
  bool found = false;
  uint32_t mask_lo = 0;
  uint32_t mask_hi = 0;
  uint32_t norm_shift = 0;
  gtcdc_internal::Build2FMasks(cfg.avg_size, cfg.nc_level, &mask_lo, &mask_hi,
                               &norm_shift);
  uint32_t h_gear_out = 0;
  uint32_t h_norm_out = 0;
  const bool use_h = state->tf_h_valid;

  if (scan_start >= w && scan_start < cut) {
    gtcdc_internal::GtCdcScanGear2FView(
        view, scan_start, cut, w, mask_lo, mask_hi, norm_shift, cfg.beta_table,
        cfg.norm_table, &cut, &found, state->tf_gear_h, state->tf_norm_h,
        use_h, &h_gear_out, &h_norm_out, probes);
  }

  const size_t scan_end = cut;
  if (!found && remaining > cfg.max_size) {
    cut = pos + cfg.max_size;
    TwoFClearResume(state);
  } else if (!found && is_last) {
    cut = proc_len;
    TwoFClearResume(state);
  } else if (!found) {
    state->tf_gear_h = h_gear_out;
    state->tf_norm_h = h_norm_out;
    state->tf_h_valid = true;
    state->tf_scan_abs = pos_bias + scan_end;
    *stop_outer = true;
    return false;
  } else {
    TwoFClearResume(state);
  }

  *cut_out = cut;
  return true;
}

bool ProcessChunkCutGear(const GtCdcSegmentView& view, size_t proc_len, size_t pos,
                         bool is_last, const GtCdcConfig& cfg, uint32_t mask,
                         uint64_t* probes, size_t* cut_out, bool* stop_outer) {
  *stop_outer = false;
  const size_t remaining = proc_len - pos;
  if (remaining <= cfg.min_size) {
    if (is_last) {
      *cut_out = proc_len;
      return true;
    }
    *stop_outer = true;
    return false;
  }

  const uint32_t w = cfg.window_w;
  const size_t scan_start = pos + cfg.min_size;
  size_t cut = std::min(pos + cfg.max_size, proc_len);
  bool found = false;

  if (scan_start >= w && scan_start < cut) {
    gtcdc_internal::GtCdcScanGearView(view, scan_start, cut, w, mask,
                                      cfg.beta_table, &cut, &found, probes);
  }

  if (!found && remaining > cfg.max_size) {
    cut = pos + cfg.max_size;
  } else if (!found && is_last) {
    cut = proc_len;
  } else if (!found) {
    *stop_outer = true;
    return false;
  }

  *cut_out = cut;
  return true;
}

bool ProcessChunkCut(const GtCdcSegmentView& view, size_t proc_len, size_t pos,
                     bool is_last, const GtCdcConfig& cfg, uint32_t mask,
                     const uint32_t pow[gtcdc_internal::kAlphaPowTableSize],
                     GtCdcStreamProfile* profile, size_t* cut_out,
                     bool* stop_outer) {
  *stop_outer = false;
  const size_t remaining = proc_len - pos;
  if (remaining <= cfg.min_size) {
    if (is_last) {
      *cut_out = proc_len;
      return true;
    }
    *stop_outer = true;
    return false;
  }

  const size_t scan_start = pos + cfg.min_size;
  size_t cut = std::min(pos + cfg.max_size, proc_len);
  bool found = false;
  gtcdc_internal::GtCdcScanStats scan_stats{};
  gtcdc_internal::GtCdcScanStats* stats_ptr =
      profile ? &scan_stats : nullptr;

  const uint32_t h =
      FeedViewRangeCompose(view, pos, cfg.min_size, 0, cfg, pow, profile);

  if (scan_start < cut) {
    GtCdcScanView(view, scan_start, cut, h, cfg, mask, pow, &cut, &found,
                  nullptr, stats_ptr);
  }

  if (profile && stats_ptr) {
    profile->blocks_composed += scan_stats.blocks_composed;
    profile->vector8_groups += scan_stats.vector8_groups;
  }

  if (!found && remaining > cfg.max_size) {
    cut = pos + cfg.max_size;
  } else if (!found && is_last) {
    cut = proc_len;
  } else if (!found) {
    *stop_outer = true;
    return false;
  }

  *cut_out = cut;
  return true;
}

Status ChunkCarryPrefixGearVirtual(GtCdcStreamState* state,
                                   const GtCdcSegmentView& view, size_t proc_len,
                                   bool is_last,
                                   std::vector<ChunkDescriptor>* out_chunks) {
  if (!state || proc_len == 0) return Status::Ok();
  if (state->digest_base == nullptr) {
    return Status::InvalidArgument(
        "G-TCDC streaming requires digest_base; no copy fallback");
  }

  const GtCdcConfig& cfg = state->config;
  const bool profile_enabled = StreamProfileEnabled();
  GtCdcStreamProfile* profile = profile_enabled ? &state->profile : nullptr;
  uint64_t* probes = &state->profile.gtcdc_scan_probes;

  if (proc_len <= cfg.min_size) {
    if (!is_last) return Status::Ok();
    std::vector<size_t> offsets{0};
    std::vector<uint32_t> lengths{static_cast<uint32_t>(proc_len)};
    std::vector<ChunkDescriptor> local;
    const Status hash_st = HashRegionsWithDigestBase(
        cfg.digest_algo, state->digest_base, state->logical_base, offsets,
        lengths, &local);
    if (!hash_st.ok()) return hash_st;
    for (auto& d : local) {
      d.offset += state->logical_base;
      out_chunks->push_back(d);
    }
    state->logical_base += proc_len;
    SetCarryTail(state, view, proc_len, profile);
    return Status::Ok();
  }

  const uint32_t mask = gtcdc_internal::MaskForAvg(cfg.avg_size);
  const uint32_t norm_mask =
      cfg.kernel == GtCdcKernel::kNative
          ? gtcdc_internal::BuildNormMask(cfg.avg_size, cfg.nc_level)
          : 0;
  const uint32_t w = cfg.window_w;
  const uint32_t* gear = cfg.beta_table;
  const bool use_nc = cfg.kernel == GtCdcKernel::kNative && norm_mask != 0;
  const bool use_angear = cfg.kernel == GtCdcKernel::kAnGear;
  const bool use_2f = cfg.kernel == GtCdcKernel::kTwoFGear;
  const size_t pos_bias = state->logical_base;
  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  offsets.reserve(proc_len / cfg.min_size + 1);
  lengths.reserve(offsets.capacity());

  size_t pos = 0;
  {
    ScopedStreamTimer scan_timer(profile ? &profile->gtcdc_scan_ns : nullptr);
    while (pos < proc_len) {
      if (use_2f) {
        size_t cut = 0;
        bool stop = false;
        if (!Process2FGearChunkCut(state, view, proc_len, pos, is_last, cfg,
                                   pos_bias, probes, &cut, &stop)) {
          if (stop) break;
          continue;
        }
        offsets.push_back(pos);
        lengths.push_back(static_cast<uint32_t>(cut - pos));
        pos = cut;
        continue;
      }
      if (use_angear) {
        size_t cut = 0;
        bool stop = false;
        if (!ProcessAnGearChunkCut(state, view, proc_len, pos, is_last, cfg,
                                   pos_bias, probes, &cut, &stop)) {
          if (stop) break;
          continue;
        }
        offsets.push_back(pos);
        lengths.push_back(static_cast<uint32_t>(cut - pos));
        pos = cut;
        continue;
      }

      if (view.len0 > 0 && pos >= view.len0) {
        while (pos < proc_len) {
          const size_t remaining = proc_len - pos;
          if (remaining <= cfg.min_size) {
            if (is_last) {
              offsets.push_back(pos);
              lengths.push_back(static_cast<uint32_t>(remaining));
            }
            break;
          }

          const size_t scan_start = pos + cfg.min_size;
          size_t cut = std::min(pos + cfg.max_size, proc_len);
          bool found = false;

          if (scan_start >= w && scan_start < cut &&
              scan_start >= view.len0 && cut <= view.len0 + view.len1) {
            size_t rel_cut = cut - view.len0;
            if (use_nc) {
              gtcdc_internal::ScanGearCutNc(
                  view.seg1, scan_start - view.len0, rel_cut, w, mask, norm_mask,
                  gear, &rel_cut, &found, probes);
            } else {
              gtcdc_internal::ScanGearCut(view.seg1, scan_start - view.len0,
                                          rel_cut, w, mask, gear, &rel_cut,
                                          &found, probes);
            }
            if (found) cut = rel_cut + view.len0;
          }

          if (!found && remaining > cfg.max_size) {
            cut = pos + cfg.max_size;
          } else if (!found && is_last) {
            cut = proc_len;
          } else if (!found) {
            break;
          }

          offsets.push_back(pos);
          lengths.push_back(static_cast<uint32_t>(cut - pos));
          pos = cut;
        }
        break;
      }

      const size_t remaining = proc_len - pos;
      if (remaining <= cfg.min_size) {
        if (is_last) {
          offsets.push_back(pos);
          lengths.push_back(static_cast<uint32_t>(remaining));
        }
        break;
      }

      const size_t scan_start = pos + cfg.min_size;
      size_t cut = std::min(pos + cfg.max_size, proc_len);
      bool found = false;

      if (scan_start >= w && scan_start < cut) {
        if (view.len0 == 0) {
          if (use_nc) {
            gtcdc_internal::ScanGearCutNc(view.seg1, scan_start, cut, w, mask,
                                          norm_mask, gear, &cut, &found,
                                          probes);
          } else {
            gtcdc_internal::ScanGearCut(view.seg1, scan_start, cut, w, mask,
                                        gear, &cut, &found, probes);
          }
        } else if (scan_start >= view.len0 && cut <= view.len0 + view.len1) {
          size_t rel_cut = cut - view.len0;
          if (use_nc) {
            gtcdc_internal::ScanGearCutNc(view.seg1, scan_start - view.len0,
                                          rel_cut, w, mask, norm_mask, gear,
                                          &rel_cut, &found, probes);
          } else {
            gtcdc_internal::ScanGearCut(view.seg1, scan_start - view.len0,
                                        rel_cut, w, mask, gear, &rel_cut,
                                        &found, probes);
          }
          if (found) cut = rel_cut + view.len0;
        } else if (use_nc) {
          gtcdc_internal::GtCdcScanGearNcView(view, scan_start, cut, w, mask,
                                              norm_mask, gear, &cut, &found,
                                              probes);
        } else {
          gtcdc_internal::GtCdcScanGearView(view, scan_start, cut, w, mask,
                                            gear, &cut, &found, probes);
        }
      }

      if (!found && remaining > cfg.max_size) {
        cut = pos + cfg.max_size;
      } else if (!found && is_last) {
        cut = proc_len;
      } else if (!found) {
        break;
      }

      offsets.push_back(pos);
      lengths.push_back(static_cast<uint32_t>(cut - pos));
      pos = cut;
    }
  }

  if (offsets.empty()) {
    if (use_angear || use_2f) state->logical_base += pos;
    SetCarryTail(state, view, pos, profile);
    return Status::Ok();
  }

  std::vector<ChunkDescriptor> local;
  {
    ScopedStreamTimer digest_timer(profile ? &profile->digest_ns : nullptr);
    const Status hash_st = HashRegionsWithDigestBase(
        cfg.digest_algo, state->digest_base, state->logical_base, offsets,
        lengths, &local);
    if (!hash_st.ok()) return hash_st;
  }
  for (auto& d : local) {
    d.offset += state->logical_base;
    out_chunks->push_back(d);
  }

  state->logical_base += pos;
  SetCarryTail(state, view, pos, profile);
  return Status::Ok();
}

Status ChunkCarryPrefixVirtual(GtCdcStreamState* state,
                               const GtCdcSegmentView& view, size_t proc_len,
                               bool is_last, std::vector<ChunkDescriptor>* out_chunks) {
  if (!state || proc_len == 0) return Status::Ok();
  if (state->digest_base == nullptr) {
    return Status::InvalidArgument(
        "G-TCDC streaming requires digest_base; no copy fallback");
  }

  if (GtCdcUsesGearFamily(state->config.kernel)) {
    return ChunkCarryPrefixGearVirtual(state, view, proc_len, is_last,
                                       out_chunks);
  }

  const GtCdcConfig& cfg = state->config;
  const bool profile_enabled = StreamProfileEnabled();
  GtCdcStreamProfile* profile = profile_enabled ? &state->profile : nullptr;
  const uint32_t mask = gtcdc_internal::MaskForAvg(cfg.avg_size);

  if (proc_len <= cfg.min_size) {
    if (!is_last) return Status::Ok();
    std::vector<size_t> offsets{0};
    std::vector<uint32_t> lengths{static_cast<uint32_t>(proc_len)};
    std::vector<ChunkDescriptor> local;
    const Status hash_st = HashRegionsWithDigestBase(
        cfg.digest_algo, state->digest_base, state->logical_base, offsets,
        lengths, &local);
    if (!hash_st.ok()) return hash_st;
    for (auto& d : local) {
      d.offset += state->logical_base;
      out_chunks->push_back(d);
    }
    state->logical_base += proc_len;
    SetCarryTail(state, view, proc_len, profile);
    return Status::Ok();
  }

  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  offsets.reserve(proc_len / cfg.min_size + 1);
  lengths.reserve(offsets.capacity());

  size_t pos = 0;
  {
    ScopedStreamTimer scan_timer(profile ? &profile->gtcdc_scan_ns : nullptr);
    while (pos < proc_len) {
      if (view.len0 > 0 && pos >= view.len0) {
        while (pos < proc_len) {
          size_t cut = 0;
          bool stop = false;
          if (!ProcessChunkCut(view, proc_len, pos, is_last, cfg, mask,
                               state->alpha_pow, profile, &cut, &stop)) {
            if (stop) break;
            continue;
          }
          offsets.push_back(pos);
          lengths.push_back(static_cast<uint32_t>(cut - pos));
          pos = cut;
        }
        break;
      }

      size_t cut = 0;
      bool stop = false;
      if (!ProcessChunkCut(view, proc_len, pos, is_last, cfg, mask,
                           state->alpha_pow, profile, &cut, &stop)) {
        if (stop) break;
        continue;
      }
      offsets.push_back(pos);
      lengths.push_back(static_cast<uint32_t>(cut - pos));
      pos = cut;
    }
  }

  if (offsets.empty()) {
    SetCarryTail(state, view, pos, profile);
    return Status::Ok();
  }

  std::vector<ChunkDescriptor> local;
  {
    ScopedStreamTimer digest_timer(profile ? &profile->digest_ns : nullptr);
    const Status hash_st = HashRegionsWithDigestBase(
        cfg.digest_algo, state->digest_base, state->logical_base, offsets,
        lengths, &local);
    if (!hash_st.ok()) return hash_st;
  }
  for (auto& d : local) {
    d.offset += state->logical_base;
    out_chunks->push_back(d);
  }

  state->logical_base += pos;
  SetCarryTail(state, view, pos, profile);
  return Status::Ok();
}

}  // namespace

void GtCdcStreamInit(GtCdcStreamState* state, GtCdcConfig config) {
  if (!state) return;
  state->config = config;
  state->carry.clear();
  state->logical_base = 0;
  state->stream_offset = 0;
  state->digest_base = nullptr;
  ResetGtCdcStreamProfile(&state->profile);
  AnGearClearChunk(state);
  TwoFClearResume(state);
  gtcdc_internal::InitGearTableForConfig(&state->config);
  gtcdc_internal::InitAlphaPowTable(state->config.alpha, state->alpha_pow);
  state->tables_ready = true;
}

Status GtCdcStreamFeed(GtCdcStreamState* state, const uint8_t* data, size_t len,
                       bool is_last, std::vector<ChunkDescriptor>* out_chunks) {
  if (!state || !out_chunks) return Status::InvalidArgument("null streaming");
  if (!state->tables_ready) GtCdcStreamInit(state, state->config);
  if (len == 0 && !is_last) return Status::Ok();

  GtCdcSegmentView view{};
  view.seg0 = state->carry.empty() ? nullptr : state->carry.data();
  view.len0 = state->carry.size();
  view.seg1 = data;
  view.len1 = len;
  state->stream_offset += len;

  size_t proc_len = view.size();
  if (!is_last) {
    if (proc_len <= state->config.min_size) return Status::Ok();
    proc_len -= state->config.min_size;
  }

  return ChunkCarryPrefixVirtual(state, view, proc_len, is_last, out_chunks);
}

Status GtCdcStreamFinish(GtCdcStreamState* state,
                         std::vector<ChunkDescriptor>* out_chunks) {
  if (!state || !out_chunks) return Status::InvalidArgument("null finish");
  if (state->carry.empty()) return Status::Ok();

  GtCdcSegmentView view{};
  view.seg0 = state->carry.data();
  view.len0 = state->carry.size();
  return ChunkCarryPrefixVirtual(state, view, view.len0, true, out_chunks);
}

void AccumulateGtCdcStreamProfile(const GtCdcStreamProfile& src,
                                  GtCdcStreamProfile* dst) {
  if (!dst) return;
  dst->carry_copy_ns += src.carry_copy_ns;
  dst->gtcdc_scan_ns += src.gtcdc_scan_ns;
  dst->digest_ns += src.digest_ns;
  dst->blocks_composed += src.blocks_composed;
  dst->vector8_groups += src.vector8_groups;
  dst->gtcdc_scan_probes += src.gtcdc_scan_probes;
}

}  // namespace ebbackup
