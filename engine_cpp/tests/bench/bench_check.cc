#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/chunk/rolling_checksum.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace {

struct BenchFloors {
  double fastcdc_mbps_min{10.0};
  double hcrbo_incr_mbps_min{10.0};
  double reuse_pct_min{90.0};
  double pipeline_ratio_min{0.90};
};

std::string MakeSyntheticData(size_t size, uint8_t seed) {
  std::string data(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<char>((seed + i * 17 + (i >> 8)) & 0xFF);
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
  auto parse_key = [&](const char* key, double* out) {
    const std::string needle = std::string("\"") + key + "\":";
    const size_t pos = content.find(needle);
    if (pos == std::string::npos) return;
    *out = std::strtod(content.c_str() + pos + needle.size(), nullptr);
  };
  parse_key("fastcdc_mbps_min", &floors->fastcdc_mbps_min);
  parse_key("hcrbo_incr_mbps_min", &floors->hcrbo_incr_mbps_min);
  parse_key("reuse_pct_min", &floors->reuse_pct_min);
  parse_key("pipeline_ratio_min", &floors->pipeline_ratio_min);
  return true;
}

BenchFloors LoadFloors() {
  BenchFloors floors{};
  const char* env = std::getenv("EB_BENCH_FLOOR_PATH");
  if (env && env[0] != '\0') {
    (void)ParseFloorsFile(env, &floors);
    return floors;
  }
  const std::string rel =
      (std::filesystem::path("engine_cpp") / "bench" / "baselines" /
       "ci_floor.json")
          .string();
  if (!ParseFloorsFile(rel, &floors)) {
    (void)ParseFloorsFile("bench/baselines/ci_floor.json", &floors);
  }
  return floors;
}

bool RunChunkBench(const std::vector<uint8_t>& data, size_t delta_offset,
                   BenchFloors* floors, double* reuse_pct_out) {
  const double mb =
      static_cast<double>(data.size()) / (1024.0 * 1024.0);

  ebbackup::FastCdcSlice fast;
  std::vector<ebbackup::ChunkDescriptor> fast_chunks;
  const auto t0 = std::chrono::steady_clock::now();
  if (!fast.Chunk(data.data(), data.size(), &fast_chunks).ok()) return false;
  const auto t1 = std::chrono::steady_clock::now();
  const double fast_sec = std::chrono::duration<double>(t1 - t0).count();
  const double fast_mbps = fast_sec > 0 ? mb / fast_sec : 0.0;

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
  const double hcrbo_mbps = hcrbo_sec > 0 ? mb / hcrbo_sec : 0.0;
  const double reuse =
      full.empty() ? 0.0
                   : 100.0 * stats.chunks_reused_from_cfi / full.size();
  if (reuse_pct_out) *reuse_pct_out = reuse;

  std::printf(
      "bench_check L1: fastcdc_mbps=%.2f hcrbo_incr_mbps=%.2f reuse_pct=%.1f "
      "rolling_skip_hits=%llu\n",
      fast_mbps, hcrbo_mbps, reuse,
      static_cast<unsigned long long>(stats.cfi_rolling_skip_hits));

  if (fast_mbps < floors->fastcdc_mbps_min) {
    std::fprintf(stderr, "fastcdc_mbps %.2f below floor %.2f\n", fast_mbps,
                 floors->fastcdc_mbps_min);
    return false;
  }
  if (hcrbo_mbps < floors->hcrbo_incr_mbps_min) {
    std::fprintf(stderr, "hcrbo_incr_mbps %.2f below floor %.2f\n", hcrbo_mbps,
                 floors->hcrbo_incr_mbps_min);
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

bool RunPipelineBench(BenchFloors* floors) {
  constexpr size_t kFileSize = 32 * 1024 * 1024;
  const auto base =
      ebbackup::test::TestOutputRoot() / "eb_bench_check_pipeline";
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

  ebbackup::BackupEngine::InitRepo(repo_seq);
  ebbackup::BackupEngine::InitRepo(repo_pipe);

  ebbackup::BackupOptions seq_opts{};
  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;

  const double seq_sec = RunBackupSeconds(repo_seq, source, seq_opts);
  const double pipe_sec = RunBackupSeconds(repo_pipe, source, pipe_opts);
  const double mb = static_cast<double>(kFileSize) / (1024.0 * 1024.0);
  const double seq_mbps = seq_sec > 0 ? mb / seq_sec : 0.0;
  const double pipe_mbps = pipe_sec > 0 ? mb / pipe_sec : 0.0;
  const double ratio = seq_mbps > 0 ? pipe_mbps / seq_mbps : 0.0;

  std::printf("bench_check L3: pipeline_ratio=%.2f seq_mbps=%.2f pipe_mbps=%.2f\n",
              ratio, seq_mbps, pipe_mbps);

  if (ratio < floors->pipeline_ratio_min) {
    std::fprintf(stderr, "pipeline_ratio %.2f below floor %.2f\n", ratio,
                 floors->pipeline_ratio_min);
    return false;
  }
  return true;
}

}  // namespace

int main() {
  BenchFloors floors = LoadFloors();
  std::printf(
      "bench_check floors: fastcdc>=%.1f hcrbo>=%.1f reuse>=%.1f ratio>=%.2f\n",
      floors.fastcdc_mbps_min, floors.hcrbo_incr_mbps_min,
      floors.reuse_pct_min, floors.pipeline_ratio_min);

  constexpr size_t kChunkSize = 64 * 1024 * 1024;
  constexpr size_t kDeltaOffset = 5 * 1024 * 1024;
  const std::string raw = MakeSyntheticData(kChunkSize, 7);
  const std::vector<uint8_t> data(raw.begin(), raw.end());

  double reuse_pct = 0.0;
  if (!RunChunkBench(data, kDeltaOffset, &floors, &reuse_pct)) {
    return 1;
  }
  if (!RunPipelineBench(&floors)) {
    return 1;
  }
  std::printf("bench_check: PASS reuse_pct=%.1f\n", reuse_pct);
  return 0;
}
