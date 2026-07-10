#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace ebbackup {

struct PipelinePhaseStats {
  std::atomic<uint64_t> read_ns{0};
  std::atomic<uint64_t> chunk_ns{0};
  std::atomic<uint64_t> encode_ns{0};
  std::atomic<uint64_t> store_ns{0};
  std::atomic<uint64_t> scan_ns{0};
  std::atomic<uint64_t> flush_ns{0};
  std::atomic<uint64_t> meta_ns{0};
  std::atomic<uint64_t> stream_cdc_ns{0};
  std::atomic<uint64_t> stream_digest_ns{0};
  std::atomic<uint64_t> stream_carry_ns{0};
  std::atomic<uint64_t> hybrid_cuts_ns{0};
  std::atomic<uint64_t> hybrid_replay_ns{0};
};

inline void ResetPipelinePhaseStats(PipelinePhaseStats* stats) {
  if (!stats) return;
  stats->read_ns.store(0);
  stats->chunk_ns.store(0);
  stats->encode_ns.store(0);
  stats->store_ns.store(0);
  stats->scan_ns.store(0);
  stats->flush_ns.store(0);
  stats->meta_ns.store(0);
  stats->stream_cdc_ns.store(0);
  stats->stream_digest_ns.store(0);
  stats->stream_carry_ns.store(0);
  stats->hybrid_cuts_ns.store(0);
  stats->hybrid_replay_ns.store(0);
}

inline void PrintPipelinePhaseStats(const PipelinePhaseStats& stats,
                                    double total_sec) {
  const auto ns_to_ms = [](uint64_t ns) {
    return static_cast<double>(ns) / 1e6;
  };
  const double read_ms = ns_to_ms(stats.read_ns.load());
  const double chunk_ms = ns_to_ms(stats.chunk_ns.load());
  const double encode_ms = ns_to_ms(stats.encode_ns.load());
  const double store_ms = ns_to_ms(stats.store_ns.load());
  const double scan_ms = ns_to_ms(stats.scan_ns.load());
  const double flush_ms = ns_to_ms(stats.flush_ns.load());
  const double meta_ms = ns_to_ms(stats.meta_ns.load());
  const double pipe_ms = read_ms + chunk_ms + encode_ms + store_ms;
  const double total_ms = total_sec * 1000.0;
  const double other_ms =
      std::max(0.0, total_ms - scan_ms - flush_ms - meta_ms);
  std::printf(
      "pipeline_profile: scan=%.1fms read=%.1fms chunk=%.1fms encode=%.1fms "
      "store=%.1fms flush=%.1fms meta=%.1fms pipe_sum=%.1fms other=%.1fms "
      "total=%.1fms\n",
      scan_ms, read_ms, chunk_ms, encode_ms, store_ms, flush_ms, meta_ms,
      pipe_ms, other_ms, total_ms);
  if (pipe_ms > total_ms * 1.05) {
    std::printf(
        "pipeline_profile_note: pipe_sum is aggregated worker time under "
        "parallelism (%.1fx wall clock); use total= for E2E throughput\n",
        pipe_ms / std::max(total_ms, 1.0));
  }

  const double stream_cdc_ms =
      ns_to_ms(stats.stream_cdc_ns.load() + stats.hybrid_cuts_ns.load());
  const double stream_digest_ms =
      ns_to_ms(stats.stream_digest_ns.load() + stats.hybrid_replay_ns.load());
  const double stream_carry_ms = ns_to_ms(stats.stream_carry_ns.load());
  if (stream_cdc_ms > 0.0 || stream_digest_ms > 0.0 || stream_carry_ms > 0.0) {
    std::printf(
        "pipeline_profile_stream_sub: cdc=%.1fms digest=%.1fms carry=%.1fms\n",
        stream_cdc_ms, stream_digest_ms, stream_carry_ms);
  }
}

inline bool PipelineProfileEnabled() {
  const char* env = std::getenv("EBBACKUP_PIPELINE_PROFILE");
  return env && env[0] == '1';
}

}  // namespace ebbackup
