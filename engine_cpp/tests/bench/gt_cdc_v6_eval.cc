#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "ebbackup/bench/throughput.h"
#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/chunk/gt_cdc.h"
#include "ebbackup/chunk/gt_cdc_internal.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/pipeline/pipeline_phase_stats.h"
#include "ebbackup/store/repo_stats.h"
#include "test_util.h"

namespace {

#if defined(_WIN32)
void SetBenchEnv(const char* key, const char* value) {
  _putenv_s(key, value);
}
#else
void SetBenchEnv(const char* key, const char* value) {
  setenv(key, value, 1);
}
#endif

struct ChunkDistStats {
  size_t chunks{0};
  double mean{0.0};
  double cv{0.0};
  double max_cut_pct{0.0};
  uint32_t max_size{0};
};

ChunkDistStats AnalyzeLengths(const std::vector<uint32_t>& lengths) {
  ChunkDistStats out{};
  if (lengths.empty()) return out;
  out.chunks = lengths.size();
  double sum = 0.0;
  uint32_t max_len = 0;
  size_t max_cut = 0;
  for (uint32_t len : lengths) {
    sum += static_cast<double>(len);
    if (len > max_len) max_len = len;
    if (len == max_len) ++max_cut;
  }
  out.mean = sum / static_cast<double>(lengths.size());
  out.max_size = max_len;
  out.max_cut_pct =
      100.0 * static_cast<double>(max_cut) / static_cast<double>(lengths.size());
  if (lengths.size() < 2 || out.mean <= 0.0) return out;
  double var = 0.0;
  for (uint32_t len : lengths) {
    const double d = static_cast<double>(len) - out.mean;
    var += d * d;
  }
  var /= static_cast<double>(lengths.size() - 1);
  out.cv = std::sqrt(var) / out.mean;
  return out;
}

void PrintChunkDist(const char* algo, const ChunkDistStats& s, uint32_t target_avg) {
  std::printf(
      "v6_eval chunk_stats: algo=%s chunks=%zu mean=%.0f target_avg=%u "
      "cv=%.3f max_cut_pct=%.1f%%\n",
      algo, s.chunks, s.mean, target_avg, s.cv, s.max_cut_pct);
}

void PrintChunkDistFile(const char* algo, const char* file, const ChunkDistStats& s,
                        uint32_t target_avg) {
  std::printf(
      "v6_eval chunk_stats: algo=%s file=%s chunks=%zu mean=%.0f target_avg=%u "
      "cv=%.3f max_cut_pct=%.1f%%\n",
      algo, file, s.chunks, s.mean, target_avg, s.cv, s.max_cut_pct);
}

ebbackup::GtCdcConfig Make2FConfig(size_t file_size);

struct TreeStats {
  size_t files{0};
  uint64_t bytes{0};
};

TreeStats ComputeTreeStats(const std::filesystem::path& root) {
  TreeStats out{};
  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) return out;
  for (const auto& ent :
       std::filesystem::recursive_directory_iterator(
           root, std::filesystem::directory_options::skip_permission_denied, ec)) {
    if (ec) {
      ec.clear();
      continue;
    }
    if (!ent.is_regular_file(ec)) continue;
    out.files += 1;
    out.bytes += static_cast<uint64_t>(ent.file_size(ec));
  }
  return out;
}

std::filesystem::path FindLargestFile(const std::filesystem::path& root) {
  std::filesystem::path best;
  uintmax_t best_size = 0;
  std::error_code ec;
  for (const auto& ent :
       std::filesystem::recursive_directory_iterator(
           root, std::filesystem::directory_options::skip_permission_denied, ec)) {
    if (ec) {
      ec.clear();
      continue;
    }
    if (!ent.is_regular_file(ec)) continue;
    const uintmax_t sz = ent.file_size(ec);
    if (sz > best_size) {
      best_size = sz;
      best = ent.path();
    }
  }
  return best;
}

bool ReadFilePrefix(const std::filesystem::path& path, size_t max_bytes,
                    std::vector<uint8_t>* out) {
  out->clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  in.seekg(0, std::ios::end);
  const std::streamoff file_size = in.tellg();
  if (file_size <= 0) return false;
  const size_t to_read =
      static_cast<size_t>(std::min<std::streamoff>(file_size,
                                                   static_cast<std::streamoff>(max_bytes)));
  out->resize(to_read);
  in.seekg(0, std::ios::beg);
  in.read(reinterpret_cast<char*>(out->data()),
          static_cast<std::streamsize>(to_read));
  return static_cast<size_t>(in.gcount()) == to_read;
}

bool RunChunkDistributionOnBuffer(const uint8_t* bytes, size_t len,
                                  const char* label) {
  ebbackup::FastCdcSlice fast;
  std::vector<size_t> off;
  std::vector<uint32_t> lens;
  if (!fast.ChunkCuts(bytes, len, &off, &lens).ok()) return false;
  PrintChunkDistFile("fastcdc", label, AnalyzeLengths(lens), fast.config().avg_size);

  ebbackup::GtCdcSlice two_f(Make2FConfig(len));
  off.clear();
  lens.clear();
  if (!two_f.ChunkCuts(bytes, len, &off, &lens).ok()) return false;
  PrintChunkDistFile("2f_gear", label, AnalyzeLengths(lens), two_f.config().avg_size);

  ebbackup::GtCdcConfig an_cfg = ebbackup::GtCdcConfigForFileSize(
      len, ebbackup::ChunkProfileMode::kAuto, ebbackup::DigestAlgo::kLegacy,
      ebbackup::GtCdcKernel::kAnGear);
  an_cfg.table_seed = 0xA4B4C4D4u;
  an_cfg.nc_level = 2;
  ebbackup::gtcdc_internal::InitGearTableForConfig(&an_cfg);
  ebbackup::GtCdcSlice an(an_cfg);
  off.clear();
  lens.clear();
  if (!an.ChunkCuts(bytes, len, &off, &lens).ok()) return false;
  PrintChunkDistFile("an_gear_ref", label, AnalyzeLengths(lens), an.config().avg_size);
  return true;
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

void WriteRandomFile(const std::string& path, size_t size, uint32_t seed) {
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path());
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

void WriteSyntheticFile(const std::string& path, size_t size, uint8_t seed) {
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path());
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

ebbackup::GtCdcConfig Make2FConfig(size_t file_size) {
  ebbackup::GtCdcConfig cfg = ebbackup::GtCdcConfigForFileSize(
      file_size, ebbackup::ChunkProfileMode::kAuto, ebbackup::DigestAlgo::kLegacy,
      ebbackup::GtCdcKernel::kTwoFGear);
  cfg.table_seed = 0xA4B4C4D4u;
  cfg.nc_level = 2;
  ebbackup::gtcdc_internal::InitGearTableForConfig(&cfg);
  return cfg;
}

bool RunChunkDistribution256MB() {
  constexpr size_t kFileSize = 256u * 1024u * 1024u;
  const std::string data = MakeRandomData(kFileSize, 43);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());

  ebbackup::FastCdcSlice fast;
  std::vector<size_t> off;
  std::vector<uint32_t> len;
  if (!fast.ChunkCuts(bytes, kFileSize, &off, &len).ok()) return false;
  PrintChunkDist("fastcdc", AnalyzeLengths(len), fast.config().avg_size);

  ebbackup::GtCdcSlice two_f(Make2FConfig(kFileSize));
  off.clear();
  len.clear();
  if (!two_f.ChunkCuts(bytes, kFileSize, &off, &len).ok()) return false;
  PrintChunkDist("2f_gear", AnalyzeLengths(len), two_f.config().avg_size);

  ebbackup::GtCdcConfig an_cfg = ebbackup::GtCdcConfigForFileSize(
      kFileSize, ebbackup::ChunkProfileMode::kAuto, ebbackup::DigestAlgo::kLegacy,
      ebbackup::GtCdcKernel::kAnGear);
  an_cfg.table_seed = 0xA4B4C4D4u;
  an_cfg.nc_level = 2;
  ebbackup::gtcdc_internal::InitGearTableForConfig(&an_cfg);
  ebbackup::GtCdcSlice an(an_cfg);
  off.clear();
  len.clear();
  if (!an.ChunkCuts(bytes, kFileSize, &off, &len).ok()) return false;
  PrintChunkDist("an_gear_ref", AnalyzeLengths(len), an.config().avg_size);
  return true;
}

bool RunRealChunkDistribution(const std::string& source_root) {
  const std::filesystem::path root(source_root);
  const TreeStats tree = ComputeTreeStats(root);
  std::printf(
      "v6_eval real_tree: path=%s files=%zu bytes=%llu (%.2f GB)\n",
      source_root.c_str(), tree.files,
      static_cast<unsigned long long>(tree.bytes),
      static_cast<double>(tree.bytes) / (1024.0 * 1024.0 * 1024.0));

  const std::filesystem::path largest = FindLargestFile(root);
  if (largest.empty()) {
    std::fprintf(stderr, "v6_eval real_chunk: no readable files under %s\n",
                 source_root.c_str());
    return false;
  }
  std::error_code ec;
  const uintmax_t file_size = std::filesystem::file_size(largest, ec);
  constexpr size_t kSampleCap = 256u * 1024u * 1024u;
  const size_t sample_size =
      static_cast<size_t>(std::min<uintmax_t>(file_size, kSampleCap));
  std::vector<uint8_t> data;
  if (!ReadFilePrefix(largest, sample_size, &data)) {
    std::fprintf(stderr, "v6_eval real_chunk: failed to read %s\n",
                 largest.string().c_str());
    return false;
  }
  std::printf(
      "v6_eval real_chunk: sample=%s size=%zu (file=%llu)\n",
      largest.string().c_str(), sample_size,
      static_cast<unsigned long long>(file_size));
  return RunChunkDistributionOnBuffer(data.data(), data.size(),
                                      largest.filename().string().c_str());
}

struct BackupRunResult {
  double seconds{0.0};
  uint64_t chunk_ns{0};
  uint64_t stream_cdc_ns{0};
  uint64_t stream_cdc_probes{0};
  uint64_t chunks_reused{0};
  uint64_t chunks_written{0};
  uint64_t bytes_processed{0};
  uint64_t bytes_before_compress{0};
  uint64_t bytes_after_compress{0};
  ebbackup::RepoStats repo_stats{};
  bool has_repo_stats{false};
};

BackupRunResult RunBackupTimed(const std::string& repo, const std::string& source,
                               ebbackup::BackupMode mode, bool gtcdc,
                               const char* tag = nullptr) {
  BackupRunResult out{};
  if (gtcdc) {
    SetBenchEnv("EBBACKUP_CDC_ALGO", "gtcdc");
  } else {
    SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  }
  SetBenchEnv("EBBACKUP_CDC_HYBRID", "0");
  SetBenchEnv("EBBACKUP_CDC_FAST_SLICE", "0");
  SetBenchEnv("EBBACKUP_PIPELINE_PROFILE", "1");

  ebbackup::BackupEngine engine(repo);
  if (!engine.Open().ok()) {
    if (tag) {
      std::fprintf(stderr, "v6_eval %s: Open failed repo=%s\n", tag, repo.c_str());
    }
    return out;
  }
  const auto t0 = std::chrono::steady_clock::now();
  ebbackup::BackupOptions opts{};
  opts.use_pipeline = true;
  const ebbackup::Status st = engine.RunBackup(source, mode, opts);
  if (!st.ok()) {
    if (tag) {
      std::fprintf(stderr, "v6_eval %s: RunBackup failed repo=%s mode=%d: %s\n",
                   tag, repo.c_str(), static_cast<int>(mode), st.message().c_str());
    }
    return out;
  }
  const auto t1 = std::chrono::steady_clock::now();
  out.seconds = std::chrono::duration<double>(t1 - t0).count();
  const auto& ps = engine.pipeline_phase_stats();
  out.chunk_ns = ps.chunk_ns.load();
  out.stream_cdc_ns = ps.stream_cdc_ns.load();
  out.stream_cdc_probes = ps.stream_cdc_probes.load();
  out.chunks_reused = engine.stats().chunks_reused;
  out.chunks_written = engine.stats().chunks_written;
  out.bytes_processed = engine.stats().bytes_processed;
  out.bytes_before_compress = engine.stats().content_class.bytes_before_compress;
  out.bytes_after_compress = engine.stats().content_class.bytes_after_compress;
  out.has_repo_stats = engine.GetRepoStats(&out.repo_stats).ok();
  return out;
}

void PrintCompressAb(const char* tag, const BackupRunResult& stream,
                     const BackupRunResult& gtcdc) {
  const auto pct_saved = [](const BackupRunResult& r) -> double {
    if (r.bytes_before_compress == 0) return 0.0;
    return 100.0 *
           (1.0 - static_cast<double>(r.bytes_after_compress) /
                      static_cast<double>(r.bytes_before_compress));
  };
  const auto repo_pct_saved = [](const BackupRunResult& r) -> double {
    if (!r.has_repo_stats || r.repo_stats.live_uncompressed_bytes == 0) {
      return 0.0;
    }
    return 100.0 * (1.0 - r.repo_stats.compress_ratio);
  };

  std::printf(
      "v6_eval %s_compress: stream_chunks=%llu gtcdc_chunks=%llu "
      "stream_unique=%llu gtcdc_unique=%llu\n",
      tag,
      static_cast<unsigned long long>(stream.chunks_written),
      static_cast<unsigned long long>(gtcdc.chunks_written),
      static_cast<unsigned long long>(
          stream.has_repo_stats ? stream.repo_stats.unique_chunks : 0),
      static_cast<unsigned long long>(
          gtcdc.has_repo_stats ? gtcdc.repo_stats.unique_chunks : 0));
  std::printf(
      "v6_eval %s_compress: stream_ratio=%.4f gtcdc_ratio=%.4f "
      "stream_saved=%.1f%% gtcdc_saved=%.1f%% "
      "gtcdc_vs_stream_compress=%.4f\n",
      tag,
      stream.has_repo_stats ? stream.repo_stats.compress_ratio : 1.0,
      gtcdc.has_repo_stats ? gtcdc.repo_stats.compress_ratio : 1.0,
      pct_saved(stream), pct_saved(gtcdc),
      stream.has_repo_stats && gtcdc.has_repo_stats &&
              stream.repo_stats.compress_ratio > 0.0
          ? gtcdc.repo_stats.compress_ratio / stream.repo_stats.compress_ratio
          : 0.0);
  std::printf(
      "v6_eval %s_compress: stream_physical=%llu gtcdc_physical=%llu "
      "stream_live=%llu gtcdc_live=%llu repo_saved=%.1f%%/%.1f%%\n",
      tag,
      static_cast<unsigned long long>(
          stream.has_repo_stats ? stream.repo_stats.physical_bytes : 0),
      static_cast<unsigned long long>(
          gtcdc.has_repo_stats ? gtcdc.repo_stats.physical_bytes : 0),
      static_cast<unsigned long long>(
          stream.has_repo_stats ? stream.repo_stats.live_bytes : 0),
      static_cast<unsigned long long>(
          gtcdc.has_repo_stats ? gtcdc.repo_stats.live_bytes : 0),
      repo_pct_saved(stream), repo_pct_saved(gtcdc));
}

void PrintPipelineAb(const char* tag, uint64_t nbytes,
                     const BackupRunResult& stream,
                     const BackupRunResult& gtcdc) {
  const double stream_MBps =
      ebbackup::bench::ThroughputMBps(nbytes, stream.seconds);
  const double gtcdc_MBps =
      ebbackup::bench::ThroughputMBps(nbytes, gtcdc.seconds);
  const double ratio = stream_MBps > 0 ? gtcdc_MBps / stream_MBps : 0.0;
  std::printf(
      "v6_eval %s: stream_MBps=%.2f gtcdc_MBps=%.2f gtcdc_vs_stream_ratio=%.3f\n",
      tag, stream_MBps, gtcdc_MBps, ratio);
  std::printf(
      "v6_eval %s: stream_cdc_ns=%llu gtcdc_cdc_ns=%llu stream_probes=%llu "
      "gtcdc_probes=%llu\n",
      tag, static_cast<unsigned long long>(stream.stream_cdc_ns),
      static_cast<unsigned long long>(gtcdc.stream_cdc_ns),
      static_cast<unsigned long long>(stream.stream_cdc_probes),
      static_cast<unsigned long long>(gtcdc.stream_cdc_probes));
  PrintCompressAb(tag, stream, gtcdc);
}

bool RunRealPipelineAb(const std::string& source_root) {
  const TreeStats tree = ComputeTreeStats(source_root);
  if (tree.bytes == 0) {
    std::fprintf(stderr, "v6_eval real_pipeline: empty or unreadable %s\n",
                 source_root.c_str());
    return false;
  }
  std::printf(
      "v6_eval real_pipeline: source=%s files=%zu bytes=%llu\n",
      source_root.c_str(), tree.files,
      static_cast<unsigned long long>(tree.bytes));

  const auto base = ebbackup::test::TestOutputRoot() / "gtcdc_v6_eval_real";
  const std::string repo_stream = (base / "repo_stream").string();
  const std::string repo_gtcdc = (base / "repo_gtcdc").string();
  std::error_code ec;
  std::filesystem::remove_all(base / "repo_stream", ec);
  std::filesystem::remove_all(base / "repo_gtcdc", ec);

  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  if (!ebbackup::test::InitDefaultRepo(repo_stream).ok()) {
    std::fprintf(stderr, "v6_eval real_pipeline: InitDefaultRepo failed\n");
    return false;
  }
  if (!ebbackup::test::InitGtCdcV6Repo(repo_gtcdc).ok()) {
    std::fprintf(stderr, "v6_eval real_pipeline: InitGtCdcV6Repo failed\n");
    return false;
  }

  const BackupRunResult stream = RunBackupTimed(
      repo_stream, source_root, ebbackup::BackupMode::kFull, false,
      "real_pipeline_stream");
  const BackupRunResult gtcdc = RunBackupTimed(
      repo_gtcdc, source_root, ebbackup::BackupMode::kFull, true,
      "real_pipeline_gtcdc");
  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  if (stream.seconds <= 0.0 || gtcdc.seconds <= 0.0) return false;

  PrintPipelineAb("real_pipeline_ab", tree.bytes, stream, gtcdc);
  if (stream.stream_cdc_probes > 0 && gtcdc.stream_cdc_probes > 0) {
    const double scan_ns_per_probe_ratio =
        static_cast<double>(gtcdc.stream_cdc_ns) /
        static_cast<double>(gtcdc.stream_cdc_probes) /
        (static_cast<double>(stream.stream_cdc_ns) /
         static_cast<double>(stream.stream_cdc_probes));
    std::printf(
        "v6_eval real_pipeline_ab: scan_ns_per_probe_ratio=%.3f "
        "(stream_probes=%llu gtcdc_probes=%llu)\n",
        scan_ns_per_probe_ratio,
        static_cast<unsigned long long>(stream.stream_cdc_probes),
        static_cast<unsigned long long>(gtcdc.stream_cdc_probes));
  }
  return true;
}

bool RunIncrAb256MB() {
  constexpr size_t kFileSize = 256u * 1024u * 1024u;
  constexpr size_t kDeltaOffset = 5u * 1024u * 1024u;
  const auto base =
      ebbackup::test::TestOutputRoot() / "gtcdc_v6_eval_incr";
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
    if (!out) {
      std::fprintf(stderr, "v6_eval l6_incr_ab: failed to write source file\n");
      return false;
    }
  }

  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  if (!ebbackup::test::InitDefaultRepo(repo_stream).ok()) {
    std::fprintf(stderr, "v6_eval l6_incr_ab: InitDefaultRepo failed\n");
    return false;
  }
  if (!ebbackup::test::InitGtCdcV6Repo(repo_gtcdc).ok()) {
    std::fprintf(stderr, "v6_eval l6_incr_ab: InitGtCdcV6Repo failed\n");
    return false;
  }

  if (RunBackupTimed(repo_stream, source, ebbackup::BackupMode::kFull, false,
                     "l6_incr_full_stream")
          .seconds <= 0.0) {
    return false;
  }
  if (RunBackupTimed(repo_gtcdc, source, ebbackup::BackupMode::kFull, true,
                     "l6_incr_full_gtcdc")
          .seconds <= 0.0) {
    return false;
  }

  {
    data[static_cast<size_t>(kDeltaOffset)] ^=
        static_cast<char>(0x3C);
    std::ofstream out(source + "/data.bin", std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) return false;
  }

  const BackupRunResult stream_incr = RunBackupTimed(
      repo_stream, source, ebbackup::BackupMode::kIncremental, false,
      "l6_incr_stream");
  const BackupRunResult gtcdc_incr = RunBackupTimed(
      repo_gtcdc, source, ebbackup::BackupMode::kIncremental, true,
      "l6_incr_gtcdc");
  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  if (stream_incr.seconds <= 0.0 || gtcdc_incr.seconds <= 0.0) return false;

  const double stream_MBps =
      ebbackup::bench::ThroughputMBps(kFileSize, stream_incr.seconds);
  const double gtcdc_MBps =
      ebbackup::bench::ThroughputMBps(kFileSize, gtcdc_incr.seconds);
  const double ratio = gtcdc_MBps > 0 ? stream_MBps / gtcdc_MBps : 0.0;
  std::printf(
      "v6_eval l6_incr_ab: stream_incr_MBps=%.2f gtcdc_incr_MBps=%.2f "
      "incr_gtcdc_vs_stream_ratio=%.3f\n",
      stream_MBps, gtcdc_MBps, ratio);
  std::printf(
      "v6_eval l6_incr_ab: stream_reused=%llu/%llu gtcdc_reused=%llu/%llu\n",
      static_cast<unsigned long long>(stream_incr.chunks_reused),
      static_cast<unsigned long long>(stream_incr.chunks_written),
      static_cast<unsigned long long>(gtcdc_incr.chunks_reused),
      static_cast<unsigned long long>(gtcdc_incr.chunks_written));
  return true;
}

bool RunPipelineAb(const char* tag, const std::filesystem::path& base,
                   uint64_t nbytes,
                   const std::function<void()>& setup_source) {
  const std::string source = (base / "source").string();
  const std::string repo_stream = (base / "repo_stream").string();
  const std::string repo_gtcdc = (base / "repo_gtcdc").string();
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(source);
  setup_source();

  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  if (!ebbackup::test::InitDefaultRepo(repo_stream).ok()) {
    std::fprintf(stderr, "v6_eval %s: InitDefaultRepo failed\n", tag);
    return false;
  }
  if (!ebbackup::test::InitGtCdcV6Repo(repo_gtcdc).ok()) {
    std::fprintf(stderr, "v6_eval %s: InitGtCdcV6Repo failed\n", tag);
    return false;
  }

  const BackupRunResult stream = RunBackupTimed(
      repo_stream, source, ebbackup::BackupMode::kFull, false,
      (std::string(tag) + "_stream").c_str());
  const BackupRunResult gtcdc = RunBackupTimed(
      repo_gtcdc, source, ebbackup::BackupMode::kFull, true,
      (std::string(tag) + "_gtcdc").c_str());
  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  if (stream.seconds <= 0.0 || gtcdc.seconds <= 0.0) return false;
  PrintPipelineAb(tag, nbytes, stream, gtcdc);
  return true;
}

bool RunPipeline2GBAb() {
  constexpr size_t kFileSize = 2u * 1024u * 1024u * 1024u;
  const auto base = ebbackup::test::TestOutputRoot() / "gtcdc_v6_eval_2gb";
  return RunPipelineAb(
      "pipeline_2gb_ab", base, kFileSize, [&]() {
        WriteSyntheticFile((base / "source" / "data.bin").string(), kFileSize,
                           44);
      });
}

bool RunPipelineMultiAb() {
  constexpr size_t kFileCount = 32;
  constexpr size_t kFileSize = 32u * 1024u * 1024u;
  const auto base = ebbackup::test::TestOutputRoot() / "gtcdc_v6_eval_multi";
  return RunPipelineAb(
      "pipeline_multi_ab", base, kFileCount * kFileSize, [&]() {
        for (size_t i = 0; i < kFileCount; ++i) {
          WriteSyntheticFile(
              (base / "source" / ("data" + std::to_string(i) + ".bin"))
                  .string(),
              kFileSize, static_cast<uint8_t>(40 + i));
        }
      });
}

bool RunPipelineMixedAb() {
  constexpr size_t kSmallCount = 800;
  constexpr size_t kSmallSize = 4u * 1024u;
  constexpr size_t kLargeSize = 512u * 1024u * 1024u;
  const auto base = ebbackup::test::TestOutputRoot() / "gtcdc_v6_eval_mixed";
  return RunPipelineAb(
      "pipeline_mixed_ab", base, kSmallCount * kSmallSize + kLargeSize, [&]() {
        for (size_t i = 0; i < kSmallCount; ++i) {
          WriteSyntheticFile(
              (base / "source" / ("small" + std::to_string(i) + ".bin"))
                  .string(),
              kSmallSize, static_cast<uint8_t>(48 + (i % 16)));
        }
        WriteSyntheticFile((base / "source" / "large.bin").string(), kLargeSize,
                           47);
      });
}

double Percentile(std::vector<double> values, double pct) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const size_t idx = std::min(
      values.size() - 1,
      static_cast<size_t>(pct * static_cast<double>(values.size() - 1)));
  return values[idx];
}

bool RunRabinMicrobench(int runs) {
  constexpr size_t kFileSize = 256u * 1024u * 1024u;
  std::string raw;
  raw.reserve(kFileSize);
  for (size_t i = 0; i < kFileSize; ++i) {
    raw.push_back(static_cast<char>((43 + i * 17 + (i >> 8)) & 0xFF));
  }
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(raw.data());

  ebbackup::GtCdcSlice gtcdc;
  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  for (int w = 0; w < 2; ++w) {
    offsets.clear();
    lengths.clear();
    (void)gtcdc.ChunkCuts(bytes, kFileSize, &offsets, &lengths);
  }

  auto bench_ms = [&]() {
    const auto t0 = std::chrono::steady_clock::now();
    offsets.clear();
    lengths.clear();
    (void)gtcdc.ChunkCuts(bytes, kFileSize, &offsets, &lengths);
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
  };

  const double scalar_ms = bench_ms();
  std::vector<double> tensor_ms;
  tensor_ms.reserve(static_cast<size_t>(runs));
  for (int r = 0; r < runs; ++r) {
    tensor_ms.push_back(bench_ms());
  }
  const double scalar_MBps =
      ebbackup::bench::ThroughputMBps(kFileSize, scalar_ms / 1000.0);
  std::vector<double> ratios;
  for (double ms : tensor_ms) {
    const double tensor_MBps =
        ebbackup::bench::ThroughputMBps(kFileSize, ms / 1000.0);
    ratios.push_back(scalar_MBps > 0 ? tensor_MBps / scalar_MBps : 0.0);
  }
  const double ratio_mean =
      ratios.empty()
          ? 0.0
          : std::accumulate(ratios.begin(), ratios.end(), 0.0) /
                static_cast<double>(ratios.size());
  std::printf(
      "v6_eval microbench_rabin: tensor_vs_scalar_mean=%.3f p50=%.3f "
      "(reference only, not v6 hot path)\n",
      ratio_mean, Percentile(ratios, 0.50));
  return true;
}

}  // namespace

bool RunSynthetic256CompressAb() {
  constexpr size_t kFileSize = 256u * 1024u * 1024u;
  const auto base = ebbackup::test::TestOutputRoot() / "gtcdc_v6_eval_compress_256";
  return RunPipelineAb("synth256", base, kFileSize, [&]() {
    WriteRandomFile((base / "source" / "data.bin").string(), kFileSize, 43);
  });
}

int main(int argc, char** argv) {
  bool quick = false;
  bool compress256 = false;
  std::string source_path;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--quick") {
      quick = true;
    } else if (arg == "--compress-256") {
      compress256 = true;
    } else if (arg == "--source" && i + 1 < argc) {
      source_path = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::printf(
          "usage: ebbackup_gtcdc_v6_eval [--quick] [--source PATH] "
          "[--compress-256]\n"
          "  --source PATH     real-file chunk stats + pipeline A/B (read-only)\n"
          "  --compress-256    synthetic 256MB compress A/B only\n");
      return 0;
    }
  }

  std::printf("v6_eval: G-TCDC v6 supplemental evaluation\n");
  SetBenchEnv("EBBACKUP_CDC_ALGO", "");
  bool ok = true;

  if (compress256) {
    ok = RunSynthetic256CompressAb() && ok;
    std::printf("v6_eval: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
  }

  if (!source_path.empty()) {
    std::printf("v6_eval: real-file mode source=%s\n", source_path.c_str());
    ok = RunRealChunkDistribution(source_path) && ok;
    ok = RunRealPipelineAb(source_path) && ok;
    std::printf("v6_eval: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
  }

  ok = RunChunkDistribution256MB() && ok;
  ok = RunRabinMicrobench(3) && ok;
  ok = RunIncrAb256MB() && ok;

  if (!quick) {
    ok = RunPipeline2GBAb() && ok;
    ok = RunPipelineMultiAb() && ok;
    ok = RunPipelineMixedAb() && ok;
  } else {
    std::printf("v6_eval: --quick skips 2GB/multi/mixed pipeline A/B\n");
  }

  std::printf("v6_eval: %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
