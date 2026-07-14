#include "ebbackup/chunk/fast_cdc_streaming.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <vector>

#include "ebbackup/chunk/fast_cdc_carry_buffer.h"
#include "ebbackup/chunk/fast_cdc_internal.h"
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

bool ScanGearCutVirtual(const StreamSegmentView& view, size_t scan_start,
                        size_t cut_limit, uint32_t w, uint32_t mask,
                        const uint32_t gear[256], size_t* out_cut, bool* found,
                        uint64_t* probes) {
  if (!out_cut || !found || scan_start >= cut_limit) return false;
  *found = false;
  if (scan_start < w) return false;

  uint32_t h = 0;
  for (size_t i = scan_start - w; i < scan_start; ++i) {
    h = (h << 1) + gear[view.at(i)];
  }
  for (size_t i = scan_start; i < cut_limit; ++i) {
    if (probes) ++*probes;
    if ((h & mask) == 0) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h = (h << 1) + gear[view.at(i)] - gear[view.at(i - w)];
  }
  return false;
}

void HashVirtualRegion(DigestAlgo algo, const StreamSegmentView& view, size_t offset,
                       uint32_t length, uint8_t hash_out[32],
                       FastCdcStreamProfile* profile) {
  if (view.region_in_seg1(offset, length)) {
    ContentHash(algo, view.seg1_ptr(offset), length, hash_out);
    return;
  }
  if (offset + length <= view.len0) {
    ContentHash(algo, view.seg0 + offset, length, hash_out);
    return;
  }
  ScopedStreamTimer timer(profile ? &profile->carry_copy_ns : nullptr);
  std::vector<uint8_t> tmp(length);
  view.copy_range(offset, length, tmp.data());
  ContentHash(algo, tmp.data(), length, hash_out);
}

void HashVirtualRegions(DigestAlgo algo, const StreamSegmentView& view,
                        const std::vector<size_t>& offsets,
                        const std::vector<uint32_t>& lengths,
                        uint64_t logical_base, const uint8_t* digest_base,
                        std::vector<ChunkDescriptor>* out,
                        FastCdcStreamProfile* profile) {
  out->clear();
  const size_t count = offsets.size();
  if (count == 0) return;

  const size_t total_bytes =
      std::accumulate(lengths.begin(), lengths.end(), size_t{0});

  out->resize(count);
  ScopedStreamTimer digest_timer(profile ? &profile->digest_ns : nullptr);

  if (digest_base != nullptr) {
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
      return;
    }
    for (size_t i = 0; i < count; ++i) {
      (*out)[i].offset = offsets[i];
      (*out)[i].length = lengths[i];
      ContentHash(algo, digest_base + logical_base + offsets[i], lengths[i],
                  (*out)[i].hash);
    }
    return;
  }

  bool all_whole_in_seg1 = view.seg1 != nullptr;
  for (size_t i = 0; i < count && all_whole_in_seg1; ++i) {
    if (!view.region_in_seg1(offsets[i], lengths[i])) {
      all_whole_in_seg1 = false;
    }
  }

  const bool use_pool = count >= kParallelHashMinChunks &&
                        total_bytes >= kParallelHashMinBytes &&
                        all_whole_in_seg1;
  if (use_pool) {
    std::vector<DigestSpan> spans(count);
    for (size_t i = 0; i < count; ++i) {
      spans[i].offset = offsets[i] - view.len0;
      spans[i].length = lengths[i];
    }
    std::vector<uint8_t> hashes(count * 32);
    DigestPool::Shared().HashRegions(algo, view.seg1, spans.data(), count,
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
    HashVirtualRegion(algo, view, offsets[i], lengths[i], (*out)[i].hash, profile);
  }
}

void SetCarryTail(FastCdcStreamState* state, const StreamSegmentView& view,
                  size_t consumed, FastCdcStreamProfile* profile) {
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

Status ChunkCarryPrefixVirtual(FastCdcStreamState* state,
                               const StreamSegmentView& view, size_t proc_len,
                               bool is_last, std::vector<ChunkDescriptor>* out_chunks) {
  if (!state || proc_len == 0) return Status::Ok();
  const FastCdcConfig& cfg = state->config;
  FastCdcStreamProfile* profile = &state->profile;

  if (proc_len <= cfg.min_size) {
    if (!is_last) return Status::Ok();
    std::vector<size_t> offsets{0};
    std::vector<uint32_t> lengths{static_cast<uint32_t>(proc_len)};
    std::vector<ChunkDescriptor> local;
    HashVirtualRegions(cfg.digest_algo, view, offsets, lengths, state->logical_base,
                     state->digest_base, &local, profile);
    for (auto& d : local) {
      d.offset += state->logical_base;
      out_chunks->push_back(d);
    }
    state->logical_base += proc_len;
    SetCarryTail(state, view, proc_len, profile);
    return Status::Ok();
  }

  const uint32_t mask = fastcdc_internal::BuildMask(cfg.avg_size);
  const uint32_t w = cfg.window_size;
  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  offsets.reserve(proc_len / cfg.min_size + 1);
  lengths.reserve(offsets.capacity());

  size_t pos = 0;
  uint64_t* probes = &profile->cdc_scan_probes;
  {
    ScopedStreamTimer cdc_timer(profile ? &profile->cdc_scan_ns : nullptr);
    while (pos < proc_len) {
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
            fastcdc_internal::ScanGearCut(view.seg1, scan_start - view.len0, rel_cut,
                                          w, mask, state->gear, &rel_cut, &found,
                                          probes);
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
          fastcdc_internal::ScanGearCut(view.seg1, scan_start, cut, w, mask,
                                          state->gear, &cut, &found, probes);
        } else if (scan_start >= view.len0 && cut <= view.len0 + view.len1) {
          size_t rel_cut = cut - view.len0;
          fastcdc_internal::ScanGearCut(view.seg1, scan_start - view.len0, rel_cut,
                                        w, mask, state->gear, &rel_cut, &found,
                                        probes);
          if (found) cut = rel_cut + view.len0;
        } else {
          ScanGearCutVirtual(view, scan_start, cut, w, mask, state->gear, &cut,
                           &found, probes);
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
    SetCarryTail(state, view, pos, profile);
    return Status::Ok();
  }

  std::vector<ChunkDescriptor> local;
  HashVirtualRegions(cfg.digest_algo, view, offsets, lengths, state->logical_base,
                   state->digest_base, &local, profile);
  for (auto& d : local) {
    d.offset += state->logical_base;
    out_chunks->push_back(d);
  }

  state->logical_base += pos;
  SetCarryTail(state, view, pos, profile);
  return Status::Ok();
}

}  // namespace

void FastCdcStreamInit(FastCdcStreamState* state, FastCdcConfig config) {
  if (!state) return;
  state->config = config;
  state->carry.clear();
  state->logical_base = 0;
  state->stream_offset = 0;
  state->digest_base = nullptr;
  ResetFastCdcStreamProfile(&state->profile);
  fastcdc_internal::InitGearTable(state->gear);
  state->gear_ready = true;
}

Status FastCdcStreamFeed(FastCdcStreamState* state, const uint8_t* data,
                         size_t len, bool is_last,
                         std::vector<ChunkDescriptor>* out_chunks) {
  if (!state || !out_chunks) return Status::InvalidArgument("null streaming");
  if (!state->gear_ready) FastCdcStreamInit(state, state->config);
  if (len == 0 && !is_last) return Status::Ok();

  StreamSegmentView view{};
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

Status FastCdcStreamFinish(FastCdcStreamState* state,
                           std::vector<ChunkDescriptor>* out_chunks) {
  if (!state || !out_chunks) return Status::InvalidArgument("null finish");
  if (state->carry.empty()) return Status::Ok();

  StreamSegmentView view{};
  view.seg0 = state->carry.data();
  view.len0 = state->carry.size();
  return ChunkCarryPrefixVirtual(state, view, view.len0, true, out_chunks);
}

void AccumulateFastCdcStreamProfile(const FastCdcStreamProfile& src,
                                    FastCdcStreamProfile* dst) {
  if (!dst) return;
  dst->carry_copy_ns += src.carry_copy_ns;
  dst->cdc_scan_ns += src.cdc_scan_ns;
  dst->digest_ns += src.digest_ns;
  dst->cdc_scan_probes += src.cdc_scan_probes;
}

}  // namespace ebbackup
