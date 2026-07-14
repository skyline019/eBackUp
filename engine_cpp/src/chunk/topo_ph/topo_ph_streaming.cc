#include "ebbackup/chunk/topo_ph_streaming.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ebbackup/common/digest.h"

namespace ebbackup {

namespace {

class ScopedStreamTimer {
 public:
  explicit ScopedStreamTimer(uint64_t* field)
      : field_(field), enabled_(field != nullptr) {
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
        "TopoPh streaming requires digest_base; no copy fallback");
  }
  out->resize(count);
  for (size_t i = 0; i < count; ++i) {
    (*out)[i].offset = offsets[i];
    (*out)[i].length = lengths[i];
    ContentHash(algo, digest_base + logical_base + offsets[i], lengths[i],
                (*out)[i].hash);
  }
  return Status::Ok();
}

void SetCarryTail(TopoPhStreamState* state, const uint8_t* data, size_t consumed,
                  size_t total, TopoPhStreamProfile* profile) {
  if (consumed >= total) {
    state->carry.clear();
    return;
  }
  const size_t tail_len = total - consumed;
  ScopedStreamTimer timer(profile ? &profile->carry_copy_ns : nullptr);
  state->carry.resize(tail_len);
  std::memcpy(state->carry.data(), data + consumed, tail_len);
  if (state->resume.in_scan && state->resume.scan_rel >= consumed) {
    state->resume.scan_rel -= consumed;
  }
}

Status ProcessFeedBuffer(TopoPhStreamState* state, const uint8_t* proc,
                         size_t proc_len, bool is_last,
                         std::vector<ChunkDescriptor>* out_chunks) {
  const TopoPhConfig& cfg = state->config;
  if (state->digest_base == nullptr) {
    return Status::InvalidArgument(
        "TopoPh streaming requires digest_base; no copy fallback");
  }

  const bool profile_enabled = StreamProfileEnabled();
  TopoPhStreamProfile* profile = profile_enabled ? &state->profile : nullptr;

  if (proc_len <= cfg.min_size) {
    if (!is_last) {
      SetCarryTail(state, proc, 0, proc_len, profile);
      return Status::Ok();
    }
    if (proc_len == 0) return Status::Ok();
    std::vector<size_t> offsets{0};
    std::vector<uint32_t> lengths{static_cast<uint32_t>(proc_len)};
    std::vector<ChunkDescriptor> local;
    ScopedStreamTimer digest_timer(profile ? &profile->digest_ns : nullptr);
    const Status hs =
        HashRegionsWithDigestBase(cfg.digest_algo, state->digest_base,
                                  state->logical_base, offsets, lengths, &local);
    if (!hs.ok()) return hs;
    for (auto& d : local) {
      d.offset += state->logical_base;
      out_chunks->push_back(d);
    }
    state->logical_base += proc_len;
    state->carry.clear();
    topo_ph_internal::ClearTopoPhResume(&state->resume);
    return Status::Ok();
  }

  uint32_t mask = 0;
  topo_ph_internal::BuildPhMasks(cfg.avg_size, cfg.topo_calib_permille,
                                 cfg.topo_shift, &mask);

  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  size_t chunk_pos = 0;
  while (chunk_pos < proc_len) {
    size_t cut = 0;
    bool done = false;
    uint64_t probes = 0;
    {
      ScopedStreamTimer scan_timer(profile ? &profile->topo_scan_ns : nullptr);
      if (!topo_ph_internal::ProcessTopoPhChunk(
              proc, proc_len, chunk_pos, is_last, cfg, mask, state->gear,
              &state->resume, &cut, &done, &probes)) {
        return Status::Internal("TopoPh ProcessTopoPhChunk failed");
      }
    }
    if (profile) profile->topo_scan_probes += probes;
    if (!done) break;
    offsets.push_back(chunk_pos);
    lengths.push_back(static_cast<uint32_t>(cut - chunk_pos));
    chunk_pos = cut;
  }

  if (!offsets.empty()) {
    std::vector<ChunkDescriptor> local;
    ScopedStreamTimer digest_timer(profile ? &profile->digest_ns : nullptr);
    const Status hs =
        HashRegionsWithDigestBase(cfg.digest_algo, state->digest_base,
                                  state->logical_base, offsets, lengths, &local);
    if (!hs.ok()) return hs;
    for (auto& d : local) {
      d.offset += state->logical_base;
      out_chunks->push_back(d);
    }
    state->logical_base += chunk_pos;
  }
  SetCarryTail(state, proc, chunk_pos, proc_len, profile);
  return Status::Ok();
}

}  // namespace

void TopoPhStreamInit(TopoPhStreamState* state, TopoPhConfig config) {
  if (!state) return;
  state->config = config;
  topo_ph_internal::InitGearTable(state->gear, config.table_seed);
  state->carry.clear();
  state->logical_base = 0;
  state->tables_ready = true;
  state->digest_base = nullptr;
  ResetTopoPhStreamProfile(&state->profile);
  topo_ph_internal::ClearTopoPhResume(&state->resume);
}

Status TopoPhStreamFeed(TopoPhStreamState* state, const uint8_t* data, size_t len,
                        bool is_last, std::vector<ChunkDescriptor>* out_chunks) {
  if (!state || !out_chunks) return Status::InvalidArgument("null state/out");
  out_chunks->clear();
  if (!state->tables_ready) {
    return Status::Internal("TopoPhStreamInit not called");
  }
  if (len == 0 && !is_last) return Status::Ok();

  std::vector<uint8_t> combined;
  const uint8_t* proc = data;
  size_t proc_len = len;
  if (!state->carry.empty()) {
    combined.resize(state->carry.size() + len);
    std::memcpy(combined.data(), state->carry.data(), state->carry.size());
    if (len > 0 && data) {
      std::memcpy(combined.data() + state->carry.size(), data, len);
    }
    proc = combined.data();
    proc_len = combined.size();
    state->carry.clear();
  }
  return ProcessFeedBuffer(state, proc, proc_len, is_last, out_chunks);
}

}  // namespace ebbackup
