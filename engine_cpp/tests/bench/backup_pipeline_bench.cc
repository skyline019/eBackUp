#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ebbackup/bench/throughput.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace {

std::string MakeSyntheticData(size_t size, uint8_t seed) {
  std::string data(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<char>((seed + i * 17 + (i >> 8)) & 0xFF);
  }
  return data;
}

double RunBackup(const std::string& repo, const std::string& source,
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

}  // namespace

int main(int argc, char** argv) {
  const size_t file_size =
      (argc >= 2) ? static_cast<size_t>(std::stoull(argv[1])) : 32 * 1024 * 1024;

  const auto base = ebbackup::test::TestOutputRoot() / "eb_pipeline_bench";
  std::filesystem::create_directories(base);
  const std::string source = (base / "source").string();
  const std::string repo_seq = (base / "repo_seq").string();
  const std::string repo_pipe = (base / "repo_pipe").string();
  const std::string repo_lz4 = (base / "repo_lz4").string();
  std::filesystem::create_directories(source);
  {
    std::ofstream out(source + "/data.bin", std::ios::binary);
    const std::string data = MakeSyntheticData(file_size, 42);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
  }

  ebbackup::test::InitDefaultRepo(repo_seq);
  ebbackup::test::InitDefaultRepo(repo_pipe);
  ebbackup::test::InitDefaultRepo(repo_lz4);

  ebbackup::BackupOptions seq_opts{};
  seq_opts.disable_pipeline = true;
  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;
  ebbackup::BackupOptions lz4_opts{};
  lz4_opts.use_lz4 = true;
  lz4_opts.use_pipeline = true;

  const double seq_sec = RunBackup(repo_seq, source, seq_opts);
  const double pipe_sec = RunBackup(repo_pipe, source, pipe_opts);
  const double lz4_sec = RunBackup(repo_lz4, source, lz4_opts);

  const uint64_t nbytes = static_cast<uint64_t>(file_size);
  const double seq_MBps = ebbackup::bench::ThroughputMBps(nbytes, seq_sec);
  const double pipe_MBps = ebbackup::bench::ThroughputMBps(nbytes, pipe_sec);
  const double lz4_MBps = ebbackup::bench::ThroughputMBps(nbytes, lz4_sec);
  const double ratio = seq_MBps > 0 ? pipe_MBps / seq_MBps : 0.0;
  const double file_mb = static_cast<double>(file_size) / ebbackup::bench::kBytesPerMB;

  std::printf(
      "bench pipeline: file_MB=%.2f sequential=%.2f MB/s pipeline=%.2f MB/s "
      "lz4_pipeline=%.2f MB/s pipeline_ratio=%.2f\n",
      file_mb, seq_MBps, pipe_MBps, lz4_MBps, ratio);

  if (ratio > 0 && ratio < 0.90) {
    std::fprintf(stderr, "pipeline throughput below 90%% of sequential\n");
    return 1;
  }
  return 0;
}
