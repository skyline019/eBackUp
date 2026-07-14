#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "ebbackup/bench/throughput.h"
#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/chunk/rolling_checksum.h"
#include "ebbackup/common/digest_sha_ni.h"
#include "ebbackup/codec/content_class.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/pipeline/pipeline_phase_stats.h"
#include "ebbackup/store/chunk_compactor.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/repo_stats.h"
#include "test_util.h"

namespace {

struct BenchFloors {
  double fastcdc_MBps_min{10.0};
  double hcrbo_incr_MBps_min{10.0};
  double reuse_pct_min{90.0};
  double pipeline_ratio_min{0.90};
  double pipeline_ratio_256MB_min{0.90};
  double backup_pipeline_MBps_min{0.0};
  double backup_pipeline_256MBps_min{0.0};
  double backup_pipeline_2GBps_min{0.0};
  double backup_pipeline_multi_MBps_min{0.0};
  double backup_pipeline_mixed_MBps_min{0.0};
  double hybrid_stream_ratio_min{0.95};
  double gtcdc_backup_pipeline_256MBps_min{0.0};
  double gtcdc_vs_stream_ratio_min{0.0};
  double gtcdc_scan_ns_ratio_max{0.0};
  double gtcdc_scan_ns_per_probe_ratio_max{0.0};
  double gtcdc_incr_vs_stream_ratio_min{0.0};
  double topo_vs_stream_ratio_min{0.0};
  double topo_scan_ns_per_probe_ratio_max{0.0};
  double topo_scan_ns_ratio_max{0.0};
  double topochain_vs_stream_ratio_min{0.0};
  double topochain_scan_ns_per_probe_ratio_max{0.0};
  double topoph_vs_stream_ratio_min{0.0};
  double topoph_scan_ns_per_probe_ratio_max{0.0};
  double topophn_vs_stream_ratio_min{0.0};
  double topophn_scan_ns_per_probe_ratio_max{0.0};
  double ampl_ratio_post_compact_max{1.05};
  double content_auto_vs_lz4_max{1.10};
};

#if defined(_WIN32)
void SetBenchEnv(const char* key, const char* value) {
  _putenv_s(key, value);
}
#else
void SetBenchEnv(const char* key, const char* value) {
  setenv(key, value, 1);
}
#endif

std::string MakeSyntheticData(size_t size, uint8_t seed) {
  std::string data(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<char>((seed + i * 17 + (i >> 8)) & 0xFF);
  }
  return data;
}

std::string MakeRandomData(size_t size, uint32_t seed) {
  std::mt19937 gen(seed);
  std::uniform_int_distribution<int> dist(0, 255);
  std::string data(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<char>(dist(gen));
  }
  return data;
}

void PopulateAnchorChecksums(const uint8_t* data, size_t len,
                             ebbackup::CfiIndex* cfi) {
  if (!cfi) return;
  for (auto& anchor : cfi->anchors) {
    if (anchor.offset + anchor.length <= len) {
      anchor.rolling_checksum =
          ebbackup::RollingChecksum(data + anchor.offset, anchor.length);
    }
  }
}

bool ParseFloorsFile(const std::string& path, BenchFloors* floors) {
  std::ifstream in(path);
  if (!in) return false;
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  auto has_key = [&](const char* key) {
    const std::string needle = std::string("\"") + key + "\":";
    return content.find(needle) != std::string::npos;
  };
  auto parse_key = [&](const char* key, double* out) {
    const std::string needle = std::string("\"") + key + "\":";
    const size_t pos = content.find(needle);
    if (pos == std::string::npos) return;
    *out = std::strtod(content.c_str() + pos + needle.size(), nullptr);
  };
  parse_key("fastcdc_MBps_min", &floors->fastcdc_MBps_min);
  parse_key("hcrbo_incr_MBps_min", &floors->hcrbo_incr_MBps_min);
  parse_key("reuse_pct_min", &floors->reuse_pct_min);
  parse_key("pipeline_ratio_min", &floors->pipeline_ratio_min);
  parse_key("pipeline_ratio_256MB_min", &floors->pipeline_ratio_256MB_min);
  parse_key("backup_pipeline_MBps_min", &floors->backup_pipeline_MBps_min);
  parse_key("backup_pipeline_256MBps_min", &floors->backup_pipeline_256MBps_min);
  parse_key("backup_pipeline_2GBps_min", &floors->backup_pipeline_2GBps_min);
  parse_key("backup_pipeline_multi_MBps_min", &floors->backup_pipeline_multi_MBps_min);
  parse_key("backup_pipeline_mixed_MBps_min", &floors->backup_pipeline_mixed_MBps_min);
  parse_key("hybrid_stream_ratio_min", &floors->hybrid_stream_ratio_min);
  parse_key("gtcdc_backup_pipeline_256MBps_min",
            &floors->gtcdc_backup_pipeline_256MBps_min);
  parse_key("gtcdc_vs_stream_ratio_min", &floors->gtcdc_vs_stream_ratio_min);
  parse_key("gtcdc_scan_ns_ratio_max", &floors->gtcdc_scan_ns_ratio_max);
  parse_key("gtcdc_scan_ns_per_probe_ratio_max",
            &floors->gtcdc_scan_ns_per_probe_ratio_max);
  parse_key("gtcdc_incr_vs_stream_ratio_min",
            &floors->gtcdc_incr_vs_stream_ratio_min);
  parse_key("topo_vs_stream_ratio_min", &floors->topo_vs_stream_ratio_min);
  parse_key("topo_scan_ns_per_probe_ratio_max",
            &floors->topo_scan_ns_per_probe_ratio_max);
  parse_key("topo_scan_ns_ratio_max", &floors->topo_scan_ns_ratio_max);
  parse_key("topochain_vs_stream_ratio_min",
            &floors->topochain_vs_stream_ratio_min);
  parse_key("topochain_scan_ns_per_probe_ratio_max",
            &floors->topochain_scan_ns_per_probe_ratio_max);
  parse_key("topoph_vs_stream_ratio_min", &floors->topoph_vs_stream_ratio_min);
  parse_key("topoph_scan_ns_per_probe_ratio_max",
            &floors->topoph_scan_ns_per_probe_ratio_max);
  parse_key("topophn_vs_stream_ratio_min", &floors->topophn_vs_stream_ratio_min);
  parse_key("topophn_scan_ns_per_probe_ratio_max",
            &floors->topophn_scan_ns_per_probe_ratio_max);
  parse_key("ampl_ratio_post_compact_max", &floors->ampl_ratio_post_compact_max);
  parse_key("content_auto_vs_lz4_max", &floors->content_auto_vs_lz4_max);

  if (has_key("fastcdc_mbps_min")) {
    double legacy = floors->fastcdc_MBps_min;
    parse_key("fastcdc_mbps_min", &legacy);
    floors->fastcdc_MBps_min = ebbackup::bench::MiBpsToMBps(legacy);
    std::fprintf(stderr,
                 "warning: fastcdc_mbps_min is deprecated (MiB/s); converted "
                 "to fastcdc_MBps_min=%.1f\n",
                 floors->fastcdc_MBps_min);
  }
  if (has_key("hcrbo_incr_mbps_min")) {
    double legacy = floors->hcrbo_incr_MBps_min;
    parse_key("hcrbo_incr_mbps_min", &legacy);
    floors->hcrbo_incr_MBps_min = ebbackup::bench::MiBpsToMBps(legacy);
    std::fprintf(stderr,
                 "warning: hcrbo_incr_mbps_min is deprecated (MiB/s); "
                 "converted to hcrbo_incr_MBps_min=%.1f\n",
                 floors->hcrbo_incr_MBps_min);
  }
  return true;
}

BenchFloors LoadFloors() {
  BenchFloors floors{};
  const char* env = std::getenv("EB_BENCH_FLOOR_PATH");
  if (env && env[0] != '\0') {
    (void)ParseFloorsFile(env, &floors);
    return floors;
  }
  const char* candidates[] = {
      "engine_cpp/bench/baselines/ci_floor.json",
      "bench/baselines/ci_floor.json",
      "../engine_cpp/bench/baselines/ci_floor.json",
      "../../engine_cpp/bench/baselines/ci_floor.json",
  };
  for (const char* path : candidates) {
    if (ParseFloorsFile(path, &floors)) return floors;
  }
  return floors;
}

bool RunChunkBench(const std::vector<uint8_t>& data, size_t delta_offset,
                   BenchFloors* floors, double* reuse_pct_out) {
#if defined(_WIN32)
  _putenv_s("EBBACKUP_DIGEST_THREADS", "4");
#else
  setenv("EBBACKUP_DIGEST_THREADS", "4", 1);
#endif
  const uint64_t nbytes = static_cast<uint64_t>(data.size());

  ebbackup::FastCdcSlice fast;
  std::vector<ebbackup::ChunkDescriptor> fast_chunks;
  const auto t0 = std::chrono::steady_clock::now();
  if (!fast.Chunk(data.data(), data.size(), &fast_chunks).ok()) return false;
  const auto t1 = std::chrono::steady_clock::now();
  const double fast_sec = std::chrono::duration<double>(t1 - t0).count();
  const double fast_MBps = ebbackup::bench::ThroughputMBps(nbytes, fast_sec);
  const double fast_MiBps = ebbackup::bench::ThroughputMiBps(nbytes, fast_sec);

  std::vector<uint8_t> mutated = data;
  if (delta_offset < mutated.size()) {
    mutated[delta_offset] ^= 0x3C;
  }

  ebbackup::EbHcrboChunker hcrbo;
  ebbackup::CfiIndex cfi;
  std::vector<ebbackup::ChunkDescriptor> full;
  if (!hcrbo.ChunkFull(mutated.data(), mutated.size(), &full, &cfi, nullptr)
           .ok()) {
    return false;
  }
  PopulateAnchorChecksums(mutated.data(), mutated.size(), &cfi);

  const auto t2 = std::chrono::steady_clock::now();
  std::vector<ebbackup::ChunkDescriptor> incr;
  ebbackup::CfiIndex cfi_out;
  ebbackup::EbHcrboStats stats{};
  if (!hcrbo.ChunkIncremental(mutated.data(), mutated.size(), cfi, &incr,
                              &cfi_out, &stats)
           .ok()) {
    return false;
  }
  const auto t3 = std::chrono::steady_clock::now();
  const double hcrbo_sec = std::chrono::duration<double>(t3 - t2).count();
  const double hcrbo_MBps = ebbackup::bench::ThroughputMBps(nbytes, hcrbo_sec);
  const double hcrbo_MiBps = ebbackup::bench::ThroughputMiBps(nbytes, hcrbo_sec);
  const double reuse =
      full.empty() ? 0.0
                   : 100.0 * stats.chunks_reused_from_cfi / full.size();
  if (reuse_pct_out) *reuse_pct_out = reuse;

  std::printf(
      "bench_check L1: fastcdc_MBps=%.2f (MiBps=%.2f) hcrbo_incr_MBps=%.2f "
      "(MiBps=%.2f) reuse_pct=%.1f rolling_skip_hits=%llu bloom_skip_hits=%llu\n",
      fast_MBps, fast_MiBps, hcrbo_MBps, hcrbo_MiBps, reuse,
      static_cast<unsigned long long>(stats.cfi_rolling_skip_hits),
      static_cast<unsigned long long>(stats.cfi_bloom_skip_hits));

  if (fast_MBps < floors->fastcdc_MBps_min) {
    std::fprintf(stderr, "fastcdc_MBps %.2f below floor %.2f\n", fast_MBps,
                 floors->fastcdc_MBps_min);
    return false;
  }
  if (hcrbo_MBps < floors->hcrbo_incr_MBps_min) {
    std::fprintf(stderr, "hcrbo_incr_MBps %.2f below floor %.2f\n", hcrbo_MBps,
                 floors->hcrbo_incr_MBps_min);
    return false;
  }
  if (reuse < floors->reuse_pct_min) {
    std::fprintf(stderr, "reuse_pct %.1f below floor %.1f\n", reuse,
                 floors->reuse_pct_min);
    return false;
  }
  return true;
}

double RunBackupSeconds(const std::string& repo, const std::string& source,
                        const ebbackup::BackupOptions& options) {
  ebbackup::BackupEngine engine(repo);
  if (!engine.Open().ok()) return 0.0;
  const auto t0 = std::chrono::steady_clock::now();
  if (!engine.RunBackup(source, ebbackup::BackupMode::kFull, options).ok()) {
    return 0.0;
  }
  const auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(t1 - t0).count();
}

struct BackupBenchResult {
  double seconds{0.0};
  uint64_t stream_cdc_ns{0};
  uint64_t stream_cdc_probes{0};
};

BackupBenchResult RunBackupBench(const std::string& repo,
                                 const std::string& source,
                                 const ebbackup::BackupOptions& options) {
  BackupBenchResult out{};
  ebbackup::BackupEngine engine(repo);
  const ebbackup::Status open_st = engine.Open();
  if (!open_st.ok()) {
    std::fprintf(stderr, "RunBackupBench: Open(%s): %s\n", repo.c_str(),
                 open_st.message().c_str());
    return out;
  }
  const auto t0 = std::chrono::steady_clock::now();
  const ebbackup::Status backup_st =
      engine.RunBackup(source, ebbackup::BackupMode::kFull, options);
  if (!backup_st.ok()) {
    std::fprintf(stderr, "RunBackupBench: RunBackup(%s): %s\n", repo.c_str(),
                 backup_st.message().c_str());
    return out;
  }
  const auto t1 = std::chrono::steady_clock::now();
  out.seconds = std::chrono::duration<double>(t1 - t0).count();
  out.stream_cdc_ns = engine.pipeline_phase_stats().stream_cdc_ns.load();
  out.stream_cdc_probes =
      engine.pipeline_phase_stats().stream_cdc_probes.load();
  return out;
}

void WriteSyntheticFile(const std::string& path, size_t size, uint8_t seed);
void WriteRandomFile(const std::string& path, size_t size, uint32_t seed);

void PrintPipelineProfilePct(const ebbackup::PipelinePhaseStats& ps) {
  const double read_ms = static_cast<double>(ps.read_ns.load()) / 1e6;
  const double chunk_ms = static_cast<double>(ps.chunk_ns.load()) / 1e6;
  const double encode_ms = static_cast<double>(ps.encode_ns.load()) / 1e6;
  const double store_ms = static_cast<double>(ps.store_ns.load()) / 1e6;
  const double flush_ms = static_cast<double>(ps.flush_ns.load()) / 1e6;
  const double pipe_ms = read_ms + chunk_ms + encode_ms + store_ms;
  if (pipe_ms <= 0.0) {
    std::printf("bench_check L5 profile_pct: (no pipeline phase data)\n");
    return;
  }
  const auto pct = [&](double ms) { return 100.0 * ms / pipe_ms; };
  std::printf(
      "bench_check L5 profile_pct: chunk=%.1f%% encode=%.1f%% store=%.1f%% "
      "read=%.1f%% flush=%.1f%%\n",
      pct(chunk_ms), pct(encode_ms), pct(store_ms), pct(read_ms),
      100.0 * flush_ms / pipe_ms);
}

void PrintStreamSubPct(const ebbackup::PipelinePhaseStats& ps) {
  const double cdc_ms = static_cast<double>(ps.stream_cdc_ns.load()) / 1e6;
  const double digest_ms = static_cast<double>(ps.stream_digest_ns.load()) / 1e6;
  const double carry_ms = static_cast<double>(ps.stream_carry_ns.load()) / 1e6;
  const double stream_ms = cdc_ms + digest_ms + carry_ms;
  if (stream_ms <= 0.0) {
    std::printf("bench_check L5 stream_sub: (no stream sub-timing)\n");
    return;
  }
  const auto pct = [&](double ms) { return 100.0 * ms / stream_ms; };
  std::printf(
      "bench_check L5 stream_sub: cdc=%.1f%% digest=%.1f%% carry=%.1f%%\n",
      pct(cdc_ms), pct(digest_ms), pct(carry_ms));
}

bool RunPipelineAdaptiveBench(BenchFloors* floors) {
  constexpr size_t kFileSize = 32 * 1024 * 1024;
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_pipeline_l3a";
  std::filesystem::create_directories(base);
  const std::string source = (base / "source").string();
  const std::string repo_seq = (base / "repo_seq").string();
  const std::string repo_pipe = (base / "repo_pipe").string();
  std::filesystem::create_directories(source);
  {
    std::ofstream out(source + "/data.bin", std::ios::binary);
    const std::string data = MakeSyntheticData(kFileSize, 42);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
  }

  ebbackup::test::InitV05Repo(repo_seq);
  ebbackup::test::InitV05Repo(repo_pipe);

  ebbackup::BackupOptions seq_opts{};
  seq_opts.disable_pipeline = true;
  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  const double seq_sec = RunBackupSeconds(repo_seq, source, seq_opts);
  const double pipe_sec = RunBackupSeconds(repo_pipe, source, pipe_opts);
  const uint64_t nbytes = static_cast<uint64_t>(kFileSize);
  const double seq_MBps = ebbackup::bench::ThroughputMBps(nbytes, seq_sec);
  const double pipe_MBps = ebbackup::bench::ThroughputMBps(nbytes, pipe_sec);
  const double ratio = seq_MBps > 0 ? pipe_MBps / seq_MBps : 0.0;

  std::printf(
      "bench_check L3a: pipeline_MBps=%.2f seq_MBps=%.2f pipeline_ratio=%.2f\n",
      pipe_MBps, seq_MBps, ratio);

  if (ratio + 1e-6 < floors->pipeline_ratio_min) {
    std::fprintf(stderr, "pipeline_ratio %.2f below floor %.2f\n", ratio,
                 floors->pipeline_ratio_min);
    return false;
  }
  if (floors->backup_pipeline_MBps_min > 0.0 &&
      pipe_MBps < floors->backup_pipeline_MBps_min) {
    std::fprintf(stderr, "backup_pipeline_MBps %.2f below floor %.2f\n",
                 pipe_MBps, floors->backup_pipeline_MBps_min);
    return false;
  }
  return true;
}

bool RunPipeline256RatioBench(BenchFloors* floors) {
  if (floors->pipeline_ratio_256MB_min <= 0.0) return true;

  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_pipeline_l3b";
  const std::string source = (base / "source").string();
  const std::string repo_seq = (base / "repo_seq").string();
  const std::string repo_pipe = (base / "repo_pipe").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  WriteSyntheticFile(source + "/data.bin", kFileSize, 45);

  ebbackup::test::InitDefaultRepo(repo_seq);
  ebbackup::test::InitDefaultRepo(repo_pipe);

  ebbackup::BackupOptions seq_opts{};
  seq_opts.disable_pipeline = true;
  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  const double seq_sec = RunBackupSeconds(repo_seq, source, seq_opts);
  const double pipe_sec = RunBackupSeconds(repo_pipe, source, pipe_opts);
  const uint64_t nbytes = static_cast<uint64_t>(kFileSize);
  const double seq_MBps = ebbackup::bench::ThroughputMBps(nbytes, seq_sec);
  const double pipe_MBps = ebbackup::bench::ThroughputMBps(nbytes, pipe_sec);
  const double ratio = seq_MBps > 0 ? pipe_MBps / seq_MBps : 0.0;

  std::printf(
      "bench_check L3b: pipeline_MBps=%.2f seq_MBps=%.2f "
      "pipeline_ratio_256=%.2f\n",
      pipe_MBps, seq_MBps, ratio);

  if (ratio + 1e-6 < floors->pipeline_ratio_256MB_min) {
    std::fprintf(stderr, "pipeline_ratio_256 %.2f below floor %.2f\n", ratio,
                 floors->pipeline_ratio_256MB_min);
    return false;
  }
  return true;
}

bool RunStorageBench(BenchFloors* floors) {
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_storage";
  std::filesystem::create_directories(base);
  const std::string repo = (base / "repo").string();
  const std::string source = (base / "source").string();
  std::filesystem::create_directories(source);
  {
    std::ofstream out(source + "/data.bin", std::ios::binary);
    const std::string data = MakeSyntheticData(512 * 1024, 55);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
  }

  std::error_code ec;
  std::filesystem::remove_all(repo, ec);
  ebbackup::test::InitV05Repo(repo);

  ebbackup::BackupEngine engine(repo);
  if (!engine.Open().ok()) return false;
  if (!engine.RunBackup(source).ok()) return false;

  ebbackup::ChunkStore store(repo + "/data/chunks");
  store.SetUseEbPack(true);
  store.SetUsePersistentIndex(true);
  store.SetTxnId(2);
  if (!store.Open().ok()) return false;
  if (!store.BeginAppendSession().ok()) return false;
  const std::string orphan = MakeSyntheticData(32 * 1024, 88);
  uint8_t orphan_hash[32];
  if (!store.Put(reinterpret_cast<const uint8_t*>(orphan.data()), orphan.size(),
                 orphan_hash)
           .ok()) {
    return false;
  }
  if (!store.Flush().ok()) return false;
  if (!store.EndAppendSession().ok()) return false;

  ebbackup::RepoStats before{};
  if (!ebbackup::ComputeRepoStats(repo, &before).ok()) return false;
  if (before.ampl_ratio <= 1.0) {
    std::fprintf(stderr, "expected ampl_ratio > 1.0 with orphan injection\n");
    return false;
  }

  ebbackup::CompactReport report{};
  if (!ebbackup::CompactChunkStore(repo, false, &report).ok()) return false;

  std::printf(
      "bench_check L4: ampl_before=%.3f ampl_after=%.3f physical=%llu live=%llu\n",
      report.ampl_ratio_before, report.ampl_ratio_after,
      static_cast<unsigned long long>(report.physical_after),
      static_cast<unsigned long long>(report.live_bytes));

  if (report.ampl_ratio_after > floors->ampl_ratio_post_compact_max) {
    std::fprintf(stderr, "ampl_ratio_after %.3f above floor %.3f\n",
                 report.ampl_ratio_after, floors->ampl_ratio_post_compact_max);
    return false;
  }
  ebbackup::BackupEngine verify_engine(repo);
  if (!verify_engine.Open().ok()) return false;
  if (!verify_engine.Verify().ok()) return false;
  return true;
}

bool RunContentClassBench(BenchFloors* floors) {
  constexpr size_t kSize = 64 * 1024 * 1024;
  const std::string raw = MakeSyntheticData(kSize, 0xAB);
  const uint8_t* data = reinterpret_cast<const uint8_t*>(raw.data());

  auto encode_seconds = [&](ebbackup::CompressMode mode) {
    ebbackup::ContentEncodeRequest req{};
    req.data = data;
    req.len = kSize;
    req.mode = mode;
    req.path_hint = "payload.bin.jpg";
    req.cpu_budget_permille = 600;
    const auto t0 = std::chrono::steady_clock::now();
    ebbackup::ContentEncodeResult out{};
    if (!ebbackup::ContentClassEncode(req, &out, nullptr).ok()) return -1.0;
    const auto t1 = std::chrono::steady_clock::now();
    if (out.codec != ebbackup::ChunkCodec::kRaw && mode == ebbackup::CompressMode::kAuto) {
      std::fprintf(stderr, "auto mode should skip incompressible payload\n");
      return -1.0;
    }
    return std::chrono::duration<double>(t1 - t0).count();
  };

  const double auto_sec = encode_seconds(ebbackup::CompressMode::kAuto);
  const double lz4_sec = encode_seconds(ebbackup::CompressMode::kLz4);
  if (auto_sec < 0 || lz4_sec <= 0) return false;

  const double ratio = auto_sec / lz4_sec;
  std::printf("bench_check L4: content_auto_vs_lz4=%.3f auto_sec=%.3f lz4_sec=%.3f\n",
              ratio, auto_sec, lz4_sec);

  if (ratio > floors->content_auto_vs_lz4_max) {
    std::fprintf(stderr, "content_auto_vs_lz4 %.3f above floor %.3f\n", ratio,
                 floors->content_auto_vs_lz4_max);
    return false;
  }
  return true;
}

bool RunPipeline256Bench(BenchFloors* floors) {
  if (floors->backup_pipeline_256MBps_min <= 0.0) return true;

  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_pipeline_l5";
  const std::string source = (base / "source").string();
  const std::string repo = (base / "repo").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  {
    std::ofstream out(source + "/data.bin", std::ios::binary);
    const std::string data = MakeSyntheticData(kFileSize, 43);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) return false;
  }

  ebbackup::test::InitDefaultRepo(repo);

  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  ebbackup::BackupEngine engine(repo);
  if (!engine.Open().ok()) return false;
  const auto t0 = std::chrono::steady_clock::now();
  if (!engine.RunBackup(source, ebbackup::BackupMode::kFull, pipe_opts).ok()) {
    return false;
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double pipe_sec = std::chrono::duration<double>(t1 - t0).count();
  const uint64_t nbytes = static_cast<uint64_t>(kFileSize);
  const double pipe_MBps = ebbackup::bench::ThroughputMBps(nbytes, pipe_sec);

  std::printf("bench_check L5: pipeline_256MBps=%.2f\n", pipe_MBps);
  PrintPipelineProfilePct(engine.pipeline_phase_stats());
  PrintStreamSubPct(engine.pipeline_phase_stats());
  std::printf("bench_check L5 sha_ni: %s\n",
              ebbackup::DigestShaNiAvailable() ? "available" : "unavailable");
  if (ebbackup::PipelineProfileEnabled()) {
    const auto& ps = engine.pipeline_phase_stats();
    const double read_ms = static_cast<double>(ps.read_ns.load()) / 1e6;
    const double chunk_ms = static_cast<double>(ps.chunk_ns.load()) / 1e6;
    const double encode_ms = static_cast<double>(ps.encode_ns.load()) / 1e6;
    const double store_ms = static_cast<double>(ps.store_ns.load()) / 1e6;
    const double flush_ms = static_cast<double>(ps.flush_ns.load()) / 1e6;
    std::printf(
        "bench_check L5 profile: read=%.1fms chunk=%.1fms encode=%.1fms "
        "store=%.1fms flush=%.1fms\n",
        read_ms, chunk_ms, encode_ms, store_ms, flush_ms);
  }

  if (pipe_MBps < floors->backup_pipeline_256MBps_min) {
    std::fprintf(stderr, "backup_pipeline_256MBps %.2f below floor %.2f\n",
                 pipe_MBps, floors->backup_pipeline_256MBps_min);
    return false;
  }

  ebbackup::BackupEngine verify(repo);
  if (!verify.Open().ok()) {
    std::fprintf(stderr, "bench_check L5: verify Open failed\n");
    return false;
  }
  const ebbackup::Status verify_st = verify.Verify();
  if (!verify_st.ok()) {
    std::fprintf(stderr, "bench_check L5: verify failed: %s\n",
                 verify_st.message().c_str());
    return false;
  }
  return true;
}

bool GtCdcProofMeasurementEnabled() {
  const char* proof = std::getenv("EB_GTCDC_PROOF");
  if (proof && std::strcmp(proof, "1") == 0) return true;
  const char* profile = std::getenv("EBBACKUP_PIPELINE_PROFILE");
  return profile && std::strcmp(profile, "1") == 0;
}

bool RunPipeline256GtCdcBench(BenchFloors* floors) {
  const bool enforce = floors->gtcdc_backup_pipeline_256MBps_min > 0.0;
  if (!enforce && !GtCdcProofMeasurementEnabled()) return true;

  SetBenchEnv("EBBACKUP_CDC_ALGO", "gtcdc");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");

  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_pipeline_gtcdc_l5";
  const std::string source = (base / "source").string();
  const std::string repo = (base / "repo").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  WriteRandomFile(source + "/data.bin", kFileSize, 43);

  ebbackup::test::InitGtCdcV6Repo(repo);

  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  ebbackup::BackupEngine engine(repo);
  if (!engine.Open().ok()) return false;
  const auto t0 = std::chrono::steady_clock::now();
  if (!engine.RunBackup(source, ebbackup::BackupMode::kFull, pipe_opts).ok()) {
    return false;
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double pipe_sec = std::chrono::duration<double>(t1 - t0).count();
  const uint64_t nbytes = static_cast<uint64_t>(kFileSize);
  const double pipe_MBps = ebbackup::bench::ThroughputMBps(nbytes, pipe_sec);

  std::printf("bench_check L5_gtcdc: gtcdc_pipeline_256MBps=%.2f\n", pipe_MBps);
  PrintPipelineProfilePct(engine.pipeline_phase_stats());
  PrintStreamSubPct(engine.pipeline_phase_stats());

  if (enforce && pipe_MBps < floors->gtcdc_backup_pipeline_256MBps_min) {
    std::fprintf(stderr, "gtcdc_backup_pipeline_256MBps %.2f below floor %.2f\n",
                 pipe_MBps, floors->gtcdc_backup_pipeline_256MBps_min);
    return false;
  }

  ebbackup::BackupEngine verify(repo);
  if (!verify.Open().ok()) return false;
  if (!verify.Verify().ok()) return false;
  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  return true;
}

bool RunPipeline256GtCdcVsStreamBench(BenchFloors* floors) {
  const bool enforce_ratio = floors->gtcdc_vs_stream_ratio_min > 0.0;
  const bool enforce_scan_per_probe =
      floors->gtcdc_scan_ns_per_probe_ratio_max > 0.0;
  const bool enforce_scan_raw = floors->gtcdc_scan_ns_ratio_max > 0.0;
  if (!enforce_ratio && !enforce_scan_per_probe && !enforce_scan_raw &&
      !GtCdcProofMeasurementEnabled()) {
    return true;
  }

  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_pipeline_l5_gtcdc_ab";
  const std::string source = (base / "source").string();
  const std::string repo_stream = (base / "repo_stream").string();
  const std::string repo_gtcdc = (base / "repo_gtcdc").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  WriteRandomFile(source + "/data.bin", kFileSize, 43);

  ebbackup::test::InitDefaultRepo(repo_stream);
  ebbackup::test::InitGtCdcV6Repo(repo_gtcdc);

  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  SetBenchEnv("EBBACKUP_FORCE_STREAM_CDC", "0");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  SetBenchEnv("EBBACKUP_PIPELINE_PROFILE", "1");
  const BackupBenchResult stream =
      RunBackupBench(repo_stream, source, pipe_opts);

  SetBenchEnv("EBBACKUP_CDC_ALGO", "gtcdc");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  const BackupBenchResult gtcdc =
      RunBackupBench(repo_gtcdc, source, pipe_opts);
  SetBenchEnv("EBBACKUP_CDC_ALGO", "");

  const uint64_t nbytes = static_cast<uint64_t>(kFileSize);
  const double stream_MBps =
      ebbackup::bench::ThroughputMBps(nbytes, stream.seconds);
  const double gtcdc_MBps =
      ebbackup::bench::ThroughputMBps(nbytes, gtcdc.seconds);
  const double ratio = stream_MBps > 0 ? gtcdc_MBps / stream_MBps : 0.0;
  const double scan_ratio =
      stream.stream_cdc_ns > 0
          ? static_cast<double>(gtcdc.stream_cdc_ns) /
                static_cast<double>(stream.stream_cdc_ns)
          : 0.0;
  const double stream_ns_per_probe =
      stream.stream_cdc_probes > 0
          ? static_cast<double>(stream.stream_cdc_ns) /
                static_cast<double>(stream.stream_cdc_probes)
          : 0.0;
  const double gtcdc_ns_per_probe =
      gtcdc.stream_cdc_probes > 0
          ? static_cast<double>(gtcdc.stream_cdc_ns) /
                static_cast<double>(gtcdc.stream_cdc_probes)
          : 0.0;
  const double scan_per_probe_ratio =
      stream_ns_per_probe > 0.0 ? gtcdc_ns_per_probe / stream_ns_per_probe : 0.0;

  std::printf(
      "bench_check L5_gtcdc_ab: stream_256MBps=%.2f gtcdc_256MBps=%.2f "
      "gtcdc_vs_stream_ratio=%.3f scan_ns_ratio=%.3f "
      "scan_ns_per_probe_ratio=%.3f\n",
      stream_MBps, gtcdc_MBps, ratio, scan_ratio, scan_per_probe_ratio);
  std::printf(
      "bench_check L5_gtcdc_ab: stream_cdc_ns=%llu gtcdc_cdc_ns=%llu "
      "stream_probes=%llu gtcdc_probes=%llu\n",
      static_cast<unsigned long long>(stream.stream_cdc_ns),
      static_cast<unsigned long long>(gtcdc.stream_cdc_ns),
      static_cast<unsigned long long>(stream.stream_cdc_probes),
      static_cast<unsigned long long>(gtcdc.stream_cdc_probes));

  if (enforce_ratio && ratio + 1e-6 < floors->gtcdc_vs_stream_ratio_min) {
    std::fprintf(stderr, "gtcdc_vs_stream_ratio %.3f below floor %.3f\n", ratio,
                 floors->gtcdc_vs_stream_ratio_min);
    return false;
  }
  if (enforce_scan_per_probe &&
      scan_per_probe_ratio >
          floors->gtcdc_scan_ns_per_probe_ratio_max + 1e-6) {
    std::fprintf(stderr,
                 "gtcdc_scan_ns_per_probe_ratio %.3f above floor %.3f\n",
                 scan_per_probe_ratio,
                 floors->gtcdc_scan_ns_per_probe_ratio_max);
    return false;
  }
  if (enforce_scan_raw && scan_ratio > floors->gtcdc_scan_ns_ratio_max + 1e-6) {
    std::fprintf(stderr, "gtcdc_scan_ns_ratio %.3f above floor %.3f\n",
                 scan_ratio, floors->gtcdc_scan_ns_ratio_max);
    return false;
  }
  return true;
}

bool RunPipeline256TopoVsStreamBench(BenchFloors* floors) {
  const bool enforce_ratio = floors->topo_vs_stream_ratio_min > 0.0;
  const bool enforce_scan_per_probe =
      floors->topo_scan_ns_per_probe_ratio_max > 0.0;
  const bool enforce_scan_raw = floors->topo_scan_ns_ratio_max > 0.0;
  if (!enforce_ratio && !enforce_scan_per_probe && !enforce_scan_raw) {
    return true;
  }

  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_pipeline_l5_topo_ab";
  const std::string source = (base / "source").string();
  const std::string repo_stream = (base / "repo_stream").string();
  const std::string repo_topo = (base / "repo_topo").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  WriteRandomFile(source + "/data.bin", kFileSize, 43);

  ebbackup::test::InitDefaultRepo(repo_stream);
  ebbackup::test::InitTopoRepo(repo_topo);

  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  SetBenchEnv("EBBACKUP_FORCE_STREAM_CDC", "0");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  SetBenchEnv("EBBACKUP_PIPELINE_PROFILE", "1");
  const BackupBenchResult stream =
      RunBackupBench(repo_stream, source, pipe_opts);

  SetBenchEnv("EBBACKUP_CDC_ALGO", "topocdc");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  const BackupBenchResult topo = RunBackupBench(repo_topo, source, pipe_opts);
  SetBenchEnv("EBBACKUP_CDC_ALGO", "");

  const uint64_t nbytes = static_cast<uint64_t>(kFileSize);
  const double stream_MBps =
      ebbackup::bench::ThroughputMBps(nbytes, stream.seconds);
  const double topo_MBps =
      ebbackup::bench::ThroughputMBps(nbytes, topo.seconds);
  const double ratio = stream_MBps > 0 ? topo_MBps / stream_MBps : 0.0;
  const double scan_ratio =
      stream.stream_cdc_ns > 0
          ? static_cast<double>(topo.stream_cdc_ns) /
                static_cast<double>(stream.stream_cdc_ns)
          : 0.0;
  const double stream_ns_per_probe =
      stream.stream_cdc_probes > 0
          ? static_cast<double>(stream.stream_cdc_ns) /
                static_cast<double>(stream.stream_cdc_probes)
          : 0.0;
  const double topo_ns_per_probe =
      topo.stream_cdc_probes > 0
          ? static_cast<double>(topo.stream_cdc_ns) /
                static_cast<double>(topo.stream_cdc_probes)
          : 0.0;
  const double scan_per_probe_ratio =
      stream_ns_per_probe > 0.0 ? topo_ns_per_probe / stream_ns_per_probe : 0.0;

  std::printf(
      "bench_check L5_topo_ab: stream_256MBps=%.2f topo_256MBps=%.2f "
      "topo_vs_stream_ratio=%.3f scan_ns_ratio=%.3f "
      "topo_scan_ns_per_probe_ratio=%.3f\n",
      stream_MBps, topo_MBps, ratio, scan_ratio, scan_per_probe_ratio);

  if (enforce_ratio && ratio + 1e-6 < floors->topo_vs_stream_ratio_min) {
    std::fprintf(stderr, "topo_vs_stream_ratio %.3f below floor %.3f\n", ratio,
                 floors->topo_vs_stream_ratio_min);
    return false;
  }
  if (enforce_scan_per_probe &&
      scan_per_probe_ratio >
          floors->topo_scan_ns_per_probe_ratio_max + 1e-6) {
    std::fprintf(stderr,
                 "topo_scan_ns_per_probe_ratio %.3f above floor %.3f\n",
                 scan_per_probe_ratio,
                 floors->topo_scan_ns_per_probe_ratio_max);
    return false;
  }
  if (enforce_scan_raw && scan_ratio > floors->topo_scan_ns_ratio_max + 1e-6) {
    std::fprintf(stderr, "topo_scan_ns_ratio %.3f above floor %.3f\n",
                 scan_ratio, floors->topo_scan_ns_ratio_max);
    return false;
  }
  return true;
}

bool RunPipeline256TopoChainVsStreamBench(BenchFloors* floors) {
  const bool enforce_ratio = floors->topochain_vs_stream_ratio_min > 0.0;
  const bool enforce_scan_per_probe =
      floors->topochain_scan_ns_per_probe_ratio_max > 0.0;
  if (!enforce_ratio && !enforce_scan_per_probe) {
    return true;
  }

  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const auto base = ebbackup::test::TestOutputRoot() /
                    "eb_bench_check_pipeline_l5_topochain_ab";
  const std::string source = (base / "source").string();
  const std::string repo_stream = (base / "repo_stream").string();
  const std::string repo_chain = (base / "repo_chain").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  WriteRandomFile(source + "/data.bin", kFileSize, 43);

  ebbackup::test::InitDefaultRepo(repo_stream);
  ebbackup::test::InitTopoChainRepo(repo_chain);

  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  SetBenchEnv("EBBACKUP_FORCE_STREAM_CDC", "0");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  SetBenchEnv("EBBACKUP_PIPELINE_PROFILE", "1");
  const BackupBenchResult stream =
      RunBackupBench(repo_stream, source, pipe_opts);

  SetBenchEnv("EBBACKUP_CDC_ALGO", "topochain");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  const BackupBenchResult chain =
      RunBackupBench(repo_chain, source, pipe_opts);
  SetBenchEnv("EBBACKUP_CDC_ALGO", "");

  const uint64_t nbytes = static_cast<uint64_t>(kFileSize);
  const double stream_MBps =
      ebbackup::bench::ThroughputMBps(nbytes, stream.seconds);
  const double chain_MBps =
      ebbackup::bench::ThroughputMBps(nbytes, chain.seconds);
  const double ratio = stream_MBps > 0 ? chain_MBps / stream_MBps : 0.0;
  const double stream_ns_per_probe =
      stream.stream_cdc_probes > 0
          ? static_cast<double>(stream.stream_cdc_ns) /
                static_cast<double>(stream.stream_cdc_probes)
          : 0.0;
  const double chain_ns_per_probe =
      chain.stream_cdc_probes > 0
          ? static_cast<double>(chain.stream_cdc_ns) /
                static_cast<double>(chain.stream_cdc_probes)
          : 0.0;
  const double scan_per_probe_ratio =
      stream_ns_per_probe > 0.0 ? chain_ns_per_probe / stream_ns_per_probe : 0.0;

  std::printf(
      "bench_check L5_topochain_ab: stream_256MBps=%.2f topochain_256MBps=%.2f "
      "topochain_vs_stream_ratio=%.3f "
      "topochain_scan_ns_per_probe_ratio=%.3f\n",
      stream_MBps, chain_MBps, ratio, scan_per_probe_ratio);

  if (enforce_ratio && ratio + 1e-6 < floors->topochain_vs_stream_ratio_min) {
    std::fprintf(stderr, "topochain_vs_stream_ratio %.3f below floor %.3f\n",
                 ratio, floors->topochain_vs_stream_ratio_min);
    return false;
  }
  if (enforce_scan_per_probe &&
      scan_per_probe_ratio >
          floors->topochain_scan_ns_per_probe_ratio_max + 1e-6) {
    std::fprintf(stderr,
                 "topochain_scan_ns_per_probe_ratio %.3f above floor %.3f\n",
                 scan_per_probe_ratio,
                 floors->topochain_scan_ns_per_probe_ratio_max);
    return false;
  }
  return true;
}

bool RunPipeline256TopoPhVsStreamBench(BenchFloors* floors) {
  const bool enforce_ratio = floors->topoph_vs_stream_ratio_min > 0.0;
  const bool enforce_scan_per_probe =
      floors->topoph_scan_ns_per_probe_ratio_max > 0.0;
  if (!enforce_ratio && !enforce_scan_per_probe) {
    return true;
  }

  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const auto base = ebbackup::test::TestOutputRoot() /
                    "eb_bench_check_pipeline_l5_topoph_ab";
  const std::string source = (base / "source").string();
  const std::string repo_stream = (base / "repo_stream").string();
  const std::string repo_ph = (base / "repo_ph").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  WriteRandomFile(source + "/data.bin", kFileSize, 47);

  ebbackup::test::InitDefaultRepo(repo_stream);
  ebbackup::test::InitTopoPhRepo(repo_ph);

  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  SetBenchEnv("EBBACKUP_FORCE_STREAM_CDC", "0");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  SetBenchEnv("EBBACKUP_PIPELINE_PROFILE", "1");
  const BackupBenchResult stream =
      RunBackupBench(repo_stream, source, pipe_opts);

  SetBenchEnv("EBBACKUP_CDC_ALGO", "topoph");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  const BackupBenchResult ph = RunBackupBench(repo_ph, source, pipe_opts);
  SetBenchEnv("EBBACKUP_CDC_ALGO", "");

  const uint64_t nbytes = static_cast<uint64_t>(kFileSize);
  const double stream_MBps =
      ebbackup::bench::ThroughputMBps(nbytes, stream.seconds);
  const double ph_MBps = ebbackup::bench::ThroughputMBps(nbytes, ph.seconds);
  const double ratio = stream_MBps > 0 ? ph_MBps / stream_MBps : 0.0;
  const double stream_ns_per_probe =
      stream.stream_cdc_probes > 0
          ? static_cast<double>(stream.stream_cdc_ns) /
                static_cast<double>(stream.stream_cdc_probes)
          : 0.0;
  const double ph_ns_per_probe =
      ph.stream_cdc_probes > 0
          ? static_cast<double>(ph.stream_cdc_ns) /
                static_cast<double>(ph.stream_cdc_probes)
          : 0.0;
  const double scan_per_probe_ratio =
      stream_ns_per_probe > 0.0 ? ph_ns_per_probe / stream_ns_per_probe : 0.0;

  std::printf(
      "bench_check L5_topoph_ab: stream_256MBps=%.2f topoph_256MBps=%.2f "
      "topoph_vs_stream_ratio=%.3f "
      "topoph_scan_ns_per_probe_ratio=%.3f\n",
      stream_MBps, ph_MBps, ratio, scan_per_probe_ratio);

  if (enforce_ratio && ratio + 1e-6 < floors->topoph_vs_stream_ratio_min) {
    std::fprintf(stderr, "topoph_vs_stream_ratio %.3f below floor %.3f\n", ratio,
                 floors->topoph_vs_stream_ratio_min);
    return false;
  }
  if (enforce_scan_per_probe &&
      scan_per_probe_ratio >
          floors->topoph_scan_ns_per_probe_ratio_max + 1e-6) {
    std::fprintf(stderr,
                 "topoph_scan_ns_per_probe_ratio %.3f above floor %.3f\n",
                 scan_per_probe_ratio,
                 floors->topoph_scan_ns_per_probe_ratio_max);
    return false;
  }
  return true;
}

bool RunPipeline256TopoPhnVsStreamBench(BenchFloors* floors) {
  const bool enforce_ratio = floors->topophn_vs_stream_ratio_min > 0.0;
  const bool enforce_scan_per_probe =
      floors->topophn_scan_ns_per_probe_ratio_max > 0.0;
  if (!enforce_ratio && !enforce_scan_per_probe) {
    return true;
  }

  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const auto base = ebbackup::test::TestOutputRoot() /
                    "eb_bench_check_pipeline_l5_topophn_ab";
  const std::string source = (base / "source").string();
  const std::string repo_stream = (base / "repo_stream").string();
  const std::string repo_phn = (base / "repo_phn").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  WriteRandomFile(source + "/data.bin", kFileSize, 47);

  // Clear before Init*: leftover EBBACKUP_CDC_ALGO (e.g. topochain) would
  // stamp Non-FastCDC features into the stream baseline repo.
  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  SetBenchEnv("EBBACKUP_FORCE_STREAM_CDC", "0");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");

  ebbackup::test::InitDefaultRepo(repo_stream);
  ebbackup::test::InitTopoPhnRepo(repo_phn);

  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  SetBenchEnv("EBBACKUP_PIPELINE_PROFILE", "1");
  const BackupBenchResult stream =
      RunBackupBench(repo_stream, source, pipe_opts);

  SetBenchEnv("EBBACKUP_CDC_ALGO", "topophn");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  const BackupBenchResult phn = RunBackupBench(repo_phn, source, pipe_opts);
  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  SetBenchEnv("EBBACKUP_PIPELINE_PROFILE", "0");

  const uint64_t nbytes = static_cast<uint64_t>(kFileSize);
  const double stream_MBps =
      ebbackup::bench::ThroughputMBps(nbytes, stream.seconds);
  const double phn_MBps = ebbackup::bench::ThroughputMBps(nbytes, phn.seconds);
  const double ratio = stream_MBps > 0 ? phn_MBps / stream_MBps : 0.0;
  const double stream_ns_per_probe =
      stream.stream_cdc_probes > 0
          ? static_cast<double>(stream.stream_cdc_ns) /
                static_cast<double>(stream.stream_cdc_probes)
          : 0.0;
  const double phn_ns_per_probe =
      phn.stream_cdc_probes > 0
          ? static_cast<double>(phn.stream_cdc_ns) /
                static_cast<double>(phn.stream_cdc_probes)
          : 0.0;
  const double scan_per_probe_ratio =
      stream_ns_per_probe > 0.0 ? phn_ns_per_probe / stream_ns_per_probe : 0.0;

  std::printf(
      "bench_check L5_topophn_ab: stream_256MBps=%.2f topophn_256MBps=%.2f "
      "topophn_vs_stream_ratio=%.3f "
      "topophn_scan_ns_per_probe_ratio=%.3f\n",
      stream_MBps, phn_MBps, ratio, scan_per_probe_ratio);

  if (enforce_ratio && ratio + 1e-6 < floors->topophn_vs_stream_ratio_min) {
    std::fprintf(stderr, "topophn_vs_stream_ratio %.3f below floor %.3f\n",
                 ratio, floors->topophn_vs_stream_ratio_min);
    return false;
  }
  if (enforce_scan_per_probe &&
      scan_per_probe_ratio >
          floors->topophn_scan_ns_per_probe_ratio_max + 1e-6) {
    std::fprintf(stderr,
                 "topophn_scan_ns_per_probe_ratio %.3f above floor %.3f\n",
                 scan_per_probe_ratio,
                 floors->topophn_scan_ns_per_probe_ratio_max);
    return false;
  }
  return true;
}

bool RunPipelineIncrGtCdcAbBench(BenchFloors* floors) {
  const bool enforce = floors->gtcdc_incr_vs_stream_ratio_min > 0.0;
  if (!enforce && !GtCdcProofMeasurementEnabled()) return true;

  constexpr size_t kFileSize = 256 * 1024 * 1024;
  constexpr size_t kDeltaOffset = 5 * 1024 * 1024;
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_pipeline_l6_gtcdc_incr";
  const std::string source = (base / "source").string();
  const std::string repo_stream = (base / "repo_stream").string();
  const std::string repo_gtcdc = (base / "repo_gtcdc").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  std::string data = MakeRandomData(kFileSize, 43);
  {
    std::ofstream out(source + "/data.bin", std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) return false;
  }

  ebbackup::test::InitDefaultRepo(repo_stream);
  ebbackup::test::InitGtCdcV6Repo(repo_gtcdc);

  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  {
    ebbackup::BackupEngine engine(repo_stream);
    if (!engine.Open().ok()) return false;
    if (!engine.RunBackup(source, ebbackup::BackupMode::kFull, pipe_opts).ok()) {
      return false;
    }
  }
  SetBenchEnv("EBBACKUP_CDC_ALGO", "gtcdc");
  {
    ebbackup::BackupEngine engine(repo_gtcdc);
    if (!engine.Open().ok()) return false;
    if (!engine.RunBackup(source, ebbackup::BackupMode::kFull, pipe_opts).ok()) {
      return false;
    }
  }

  data[kDeltaOffset] ^= 0x3C;
  {
    std::ofstream out(source + "/data.bin", std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
  }

  auto run_incr_seconds = [&](const std::string& repo, bool gtcdc) -> double {
    if (gtcdc) {
      SetBenchEnv("EBBACKUP_CDC_ALGO", "gtcdc");
    } else {
      SetBenchEnv("EBBACKUP_CDC_ALGO", "");
    }
    ebbackup::BackupEngine engine(repo);
    if (!engine.Open().ok()) return -1.0;
    const auto t0 = std::chrono::steady_clock::now();
    if (!engine.RunBackup(source, ebbackup::BackupMode::kIncremental, pipe_opts)
             .ok()) {
      return -1.0;
    }
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
  };

  const double stream_sec = run_incr_seconds(repo_stream, false);
  const double gtcdc_sec = run_incr_seconds(repo_gtcdc, true);
  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  if (stream_sec <= 0.0 || gtcdc_sec <= 0.0) return false;

  const double stream_MBps =
      ebbackup::bench::ThroughputMBps(static_cast<uint64_t>(kFileSize), stream_sec);
  const double gtcdc_MBps =
      ebbackup::bench::ThroughputMBps(static_cast<uint64_t>(kFileSize), gtcdc_sec);
  const double ratio = gtcdc_MBps > 0 ? stream_MBps / gtcdc_MBps : 0.0;

  std::printf(
      "bench_check L6_gtcdc_incr_ab: stream_incr_MBps=%.2f gtcdc_incr_MBps=%.2f "
      "incr_gtcdc_vs_stream_ratio=%.3f\n",
      stream_MBps, gtcdc_MBps, ratio);

  if (enforce && ratio + 1e-6 < floors->gtcdc_incr_vs_stream_ratio_min) {
    std::fprintf(stderr, "incr_gtcdc_vs_stream_ratio %.3f below floor %.3f\n",
                 ratio, floors->gtcdc_incr_vs_stream_ratio_min);
    return false;
  }
  return true;
}

bool RunPipeline256HybridBench(BenchFloors* floors) {
  if (floors->hybrid_stream_ratio_min <= 0.0) return true;

  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_pipeline_l5_hybrid";
  const std::string source = (base / "source").string();
  const std::string repo_stream = (base / "repo_stream").string();
  const std::string repo_hybrid = (base / "repo_hybrid").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  WriteSyntheticFile(source + "/data.bin", kFileSize, 46);

  ebbackup::test::InitDefaultRepo(repo_stream);
  ebbackup::test::InitDefaultRepo(repo_hybrid);

  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  SetBenchEnv("EBBACKUP_FORCE_STREAM_CDC", "0");
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  const double stream_sec = RunBackupSeconds(repo_stream, source, pipe_opts);

  SetBenchEnv("EBBACKUP_CDC_HYBRID", "1");
  const double hybrid_sec = RunBackupSeconds(repo_hybrid, source, pipe_opts);
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");

  const uint64_t nbytes = static_cast<uint64_t>(kFileSize);
  const double stream_MBps =
      ebbackup::bench::ThroughputMBps(nbytes, stream_sec);
  const double hybrid_MBps =
      ebbackup::bench::ThroughputMBps(nbytes, hybrid_sec);
  const double ratio = stream_MBps > 0 ? hybrid_MBps / stream_MBps : 0.0;

  std::printf(
      "bench_check L5 hybrid: stream_256MBps=%.2f hybrid_256MBps=%.2f "
      "hybrid_stream_ratio=%.2f\n",
      stream_MBps, hybrid_MBps, ratio);

  if (ratio + 1e-6 < floors->hybrid_stream_ratio_min) {
    std::fprintf(stderr, "hybrid_stream_ratio %.2f below floor %.2f\n", ratio,
                 floors->hybrid_stream_ratio_min);
    return false;
  }
  return true;
}

void WriteSyntheticFile(const std::string& path, size_t size, uint8_t seed) {
  std::ofstream out(path, std::ios::binary);
  constexpr size_t kBlock = 64u * 1024u;
  std::string block(kBlock, '\0');
  for (size_t off = 0; off < size; off += kBlock) {
    const size_t n = std::min(kBlock, size - off);
    for (size_t i = 0; i < n; ++i) {
      block[i] = static_cast<char>((seed + (off + i) * 17 + ((off + i) >> 8)) & 0xFF);
    }
    out.write(block.data(), static_cast<std::streamsize>(n));
  }
}

void WriteRandomFile(const std::string& path, size_t size, uint32_t seed) {
  std::ofstream out(path, std::ios::binary);
  constexpr size_t kBlock = 64u * 1024u;
  std::string block(kBlock, '\0');
  for (size_t off = 0; off < size; off += kBlock) {
    const size_t n = std::min(kBlock, size - off);
    std::mt19937 gen(seed + static_cast<uint32_t>(off / kBlock));
    std::uniform_int_distribution<int> dist(0, 255);
    for (size_t i = 0; i < n; ++i) {
      block[i] = static_cast<char>(dist(gen));
    }
    out.write(block.data(), static_cast<std::streamsize>(n));
  }
}

bool RunPipeline2GBBench(BenchFloors* floors) {
  if (floors->backup_pipeline_2GBps_min <= 0.0) return true;

  constexpr size_t kFileSize = 2u * 1024u * 1024u * 1024u;
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_pipeline_l6";
  const std::string source = (base / "source").string();
  const std::string repo = (base / "repo").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  WriteSyntheticFile(source + "/data.bin", kFileSize, 44);

  ebbackup::test::InitDefaultRepo(repo);

  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  const double pipe_sec = RunBackupSeconds(repo, source, pipe_opts);
  const uint64_t nbytes = static_cast<uint64_t>(kFileSize);
  const double pipe_MBps = ebbackup::bench::ThroughputMBps(nbytes, pipe_sec);

  std::printf("bench_check L6: pipeline_2GBps=%.2f\n", pipe_MBps);

  if (pipe_MBps < floors->backup_pipeline_2GBps_min) {
    std::fprintf(stderr, "backup_pipeline_2GBps %.2f below floor %.2f\n",
                 pipe_MBps, floors->backup_pipeline_2GBps_min);
    return false;
  }

  ebbackup::BackupEngine verify(repo);
  if (!verify.Open().ok()) {
    std::fprintf(stderr, "bench_check L6: verify Open failed\n");
    return false;
  }
  const ebbackup::Status verify_st = verify.Verify();
  if (!verify_st.ok()) {
    std::fprintf(stderr, "bench_check L6: verify failed: %s\n",
                 verify_st.message().c_str());
    return false;
  }
  return true;
}

bool RunPipelineMultiBench(BenchFloors* floors) {
  if (floors->backup_pipeline_multi_MBps_min <= 0.0) return true;

  constexpr size_t kFileCount = 32;
  constexpr size_t kFileSize = 32 * 1024 * 1024;
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_pipeline_l7";
  const std::string source = (base / "source").string();
  const std::string repo = (base / "repo").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  for (size_t i = 0; i < kFileCount; ++i) {
    WriteSyntheticFile(source + "/data" + std::to_string(i) + ".bin", kFileSize,
                       static_cast<uint8_t>(40 + i));
  }

  ebbackup::test::InitDefaultRepo(repo);

  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  const double pipe_sec = RunBackupSeconds(repo, source, pipe_opts);
  const uint64_t nbytes = static_cast<uint64_t>(kFileCount * kFileSize);
  const double pipe_MBps = ebbackup::bench::ThroughputMBps(nbytes, pipe_sec);

  std::printf("bench_check L7: pipeline_multi_MBps=%.2f\n", pipe_MBps);

  if (pipe_MBps < floors->backup_pipeline_multi_MBps_min) {
    std::fprintf(stderr, "backup_pipeline_multi_MBps %.2f below floor %.2f\n",
                 pipe_MBps, floors->backup_pipeline_multi_MBps_min);
    return false;
  }

  ebbackup::BackupEngine verify(repo);
  if (!verify.Open().ok()) {
    std::fprintf(stderr, "bench_check L7: verify Open failed\n");
    return false;
  }
  const ebbackup::Status verify_st = verify.Verify();
  if (!verify_st.ok()) {
    std::fprintf(stderr, "bench_check L7: verify failed: %s\n",
                 verify_st.message().c_str());
    return false;
  }
  return true;
}

bool RunPipelineMixedBench(BenchFloors* floors) {
  if (floors->backup_pipeline_mixed_MBps_min <= 0.0) return true;

  constexpr size_t kSmallCount = 800;
  constexpr size_t kSmallSize = 4 * 1024;
  constexpr size_t kLargeSize = 512u * 1024u * 1024u;
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_pipeline_l8";
  const std::string source = (base / "source").string();
  const std::string repo = (base / "repo").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  for (size_t i = 0; i < kSmallCount; ++i) {
    WriteSyntheticFile(source + "/small" + std::to_string(i) + ".bin", kSmallSize,
                       static_cast<uint8_t>(48 + (i % 16)));
  }
  WriteSyntheticFile(source + "/large.bin", kLargeSize, 47);

  ebbackup::test::InitDefaultRepo(repo);

  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  ebbackup::BackupEngine engine(repo);
  if (!engine.Open().ok()) return false;
  const auto t0 = std::chrono::steady_clock::now();
  if (!engine.RunBackup(source, ebbackup::BackupMode::kFull, pipe_opts).ok()) {
    return false;
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double pipe_sec = std::chrono::duration<double>(t1 - t0).count();
  const uint64_t nbytes =
      static_cast<uint64_t>(kSmallCount * kSmallSize + kLargeSize);
  const double pipe_MBps = ebbackup::bench::ThroughputMBps(nbytes, pipe_sec);

  std::printf("bench_check L8: pipeline_mixed_MBps=%.2f\n", pipe_MBps);
  PrintPipelineProfilePct(engine.pipeline_phase_stats());

  if (pipe_MBps < floors->backup_pipeline_mixed_MBps_min) {
    std::fprintf(stderr, "backup_pipeline_mixed_MBps %.2f below floor %.2f\n",
                 pipe_MBps, floors->backup_pipeline_mixed_MBps_min);
    return false;
  }

  if (!engine.Verify().ok()) {
    std::fprintf(stderr, "bench_check L8: verify failed\n");
    return false;
  }
  return true;
}

}  // namespace

int main() {
  BenchFloors floors = LoadFloors();
  std::printf(
      "bench_check floors: fastcdc>=%.1f MB/s hcrbo>=%.1f MB/s reuse>=%.1f%% "
      "ratio>=%.2f ratio256>=%.2f hybridRatio>=%.2f pipe32>=%.1f MB/s "
      "pipe256>=%.1f MB/s pipe2GB>=%.1f MB/s pipeMulti>=%.1f MB/s "
      "pipeMixed>=%.1f MB/s ampl_after<=%.2f auto/lz4<=%.2f\n",
      floors.fastcdc_MBps_min, floors.hcrbo_incr_MBps_min,
      floors.reuse_pct_min, floors.pipeline_ratio_min,
      floors.pipeline_ratio_256MB_min, floors.hybrid_stream_ratio_min,
      floors.backup_pipeline_MBps_min, floors.backup_pipeline_256MBps_min,
      floors.backup_pipeline_2GBps_min, floors.backup_pipeline_multi_MBps_min,
      floors.backup_pipeline_mixed_MBps_min,
      floors.ampl_ratio_post_compact_max,
      floors.content_auto_vs_lz4_max);

  constexpr size_t kChunkSize = 64 * 1024 * 1024;
  constexpr size_t kDeltaOffset = 5 * 1024 * 1024;
  const std::string raw = MakeSyntheticData(kChunkSize, 7);
  const std::vector<uint8_t> data(raw.begin(), raw.end());

  double reuse_pct = 0.0;
  if (!RunChunkBench(data, kDeltaOffset, &floors, &reuse_pct)) {
    return 1;
  }
  if (!RunPipelineAdaptiveBench(&floors)) {
    return 1;
  }
  if (!RunPipeline256RatioBench(&floors)) {
    return 1;
  }
  if (!RunPipeline256Bench(&floors)) {
    return 1;
  }
  if (!RunPipeline256GtCdcBench(&floors)) {
    return 1;
  }
  if (!RunPipeline256GtCdcVsStreamBench(&floors)) {
    return 1;
  }
  if (!RunPipeline256TopoVsStreamBench(&floors)) {
    return 1;
  }
  if (!RunPipeline256TopoChainVsStreamBench(&floors)) {
    return 1;
  }
  if (!RunPipeline256TopoPhVsStreamBench(&floors)) {
    return 1;
  }
  if (!RunPipeline256TopoPhnVsStreamBench(&floors)) {
    return 1;
  }
  if (!RunPipelineIncrGtCdcAbBench(&floors)) {
    return 1;
  }
  if (!RunPipeline256HybridBench(&floors)) {
    return 1;
  }
  if (!RunPipeline2GBBench(&floors)) {
    return 1;
  }
  if (!RunPipelineMultiBench(&floors)) {
    return 1;
  }
  if (!RunPipelineMixedBench(&floors)) {
    return 1;
  }
  if (!RunStorageBench(&floors)) {
    return 1;
  }
  if (!RunContentClassBench(&floors)) {
    return 1;
  }
  std::printf("bench_check: PASS reuse_pct=%.1f\n", reuse_pct);
  return 0;
}
