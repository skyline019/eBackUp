#include "ebbackup/chunk/topo_chain_streaming.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ebbackup/common/digest.h"

namespace ebbackup {

namespace {

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
        "TopoChain streaming requires digest_base; no copy fallback");
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

void SetCarryTail(TopoChainStreamState* state, const uint8_t* data, size_t consumed,
                  size_t total, TopoChainStreamProfile* profile) {
  if (consumed >= total) {
    state->carry.clear();
    return;
  }
  const size_t tail_len = total - consumed;
  ScopedStreamTimer timer(profile ? &profile->carry_copy_ns : nullptr);
  state->carry.resize(tail_len);
  std::memcpy(state->carry.data(), data + consumed, tail_len);
  if (state->chain_resume.in_scan && state->chain_resume.scan_rel >= consumed) {
    state->chain_resume.scan_rel -= consumed;
  }
}

Status ProcessChainFeedBuffer(TopoChainStreamState* state, const uint8_t* proc,
                              size_t proc_len, bool is_last,
                              std::vector<ChunkDescriptor>* out_chunks) {
  const TopoCdcConfig& cfg = state->config;
  if (state->digest_base == nullptr) {
    return Status::InvalidArgument(
        "TopoChain streaming requires digest_base; no copy fallback");
  }
  if (cfg.variant != TopoCdcVariant::kChain) {
    return Status::Internal("TopoChain streaming requires kChain variant");
  }

  const bool profile_enabled = StreamProfileEnabled();
  TopoChainStreamProfile* profile = profile_enabled ? &state->profile : nullptr;

  if (proc_len <= cfg.min_size) {
    if (!is_last) {
      SetCarryTail(state, proc, 0, proc_len, profile);
      return Status::Ok();
    }
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
    SetCarryTail(state, proc, proc_len, proc_len, profile);
    topo_chain_internal::ClearTopoChainResume(&state->chain_resume);
    return Status::Ok();
  }

  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  offsets.reserve(proc_len / cfg.min_size + 1);
  lengths.reserve(offsets.capacity());

  size_t pos = 0;
  {
    ScopedStreamTimer scan_timer(profile ? &profile->chain_scan_ns : nullptr);
    uint64_t* probes = profile ? &profile->chain_scan_probes : nullptr;
    while (pos < proc_len) {
      size_t cut = 0;
      bool chunk_done = false;
      topo_chain_internal::ProcessChainChunk(
          proc, proc_len, pos, false, cfg, &state->chain_resume, &cut,
          &chunk_done, probes);
      if (!chunk_done) break;
      offsets.push_back(pos);
      lengths.push_back(static_cast<uint32_t>(cut - pos));
      pos = cut;
    }
  }

  if (offsets.empty()) {
    SetCarryTail(state, proc, pos, proc_len, profile);
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
  SetCarryTail(state, proc, pos, proc_len, profile);

  if (is_last && !state->carry.empty()) {
    const uint8_t* tail = state->carry.data();
    const size_t tail_len = state->carry.size();
    size_t cut = 0;
    bool chunk_done = false;
    uint64_t probes = 0;
    topo_chain_internal::ProcessChainChunk(
        tail, tail_len, 0, true, cfg, &state->chain_resume, &cut, &chunk_done,
        &probes);
    if (profile) profile->chain_scan_probes += probes;
    if (chunk_done && cut > 0) {
      std::vector<size_t> tail_off{0};
      std::vector<uint32_t> tail_len_vec{static_cast<uint32_t>(cut)};
      std::vector<ChunkDescriptor> tail_chunks;
      const Status hash_st = HashRegionsWithDigestBase(
          cfg.digest_algo, state->digest_base, state->logical_base, tail_off,
          tail_len_vec, &tail_chunks);
      if (!hash_st.ok()) return hash_st;
      for (auto& d : tail_chunks) {
        d.offset += state->logical_base;
        out_chunks->push_back(d);
      }
      state->logical_base += cut;
    }
    state->carry.clear();
    topo_chain_internal::ClearTopoChainResume(&state->chain_resume);
  }
  return Status::Ok();
}

}  // namespace

void TopoChainStreamInit(TopoChainStreamState* state, TopoCdcConfig config) {
  if (!state) return;
  state->config = config;
  state->carry.clear();
  state->logical_base = 0;
  state->digest_base = nullptr;
  ResetTopoChainStreamProfile(&state->profile);
  topo_chain_internal::ClearTopoChainResume(&state->chain_resume);
}

Status TopoChainStreamFeed(TopoChainStreamState* state, const uint8_t* data,
                           size_t len, bool is_last,
                           std::vector<ChunkDescriptor>* out_chunks) {
  if (!state || !out_chunks) return Status::InvalidArgument("null streaming");
  if (len == 0 && !is_last) return Status::Ok();

  std::vector<uint8_t> merged;
  const uint8_t* proc = data;
  size_t proc_len = len;
  if (!state->carry.empty()) {
    merged.reserve(state->carry.size() + len);
    merged.insert(merged.end(), state->carry.begin(), state->carry.end());
    merged.insert(merged.end(), data, data + len);
    proc = merged.data();
    proc_len = merged.size();
    state->carry.clear();
  }

  return ProcessChainFeedBuffer(state, proc, proc_len, is_last, out_chunks);
}

}  // namespace ebbackup
