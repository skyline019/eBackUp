#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

  ebbackup::BackupEngine::InitRepo(repo_seq);
  ebbackup::BackupEngine::InitRepo(repo_pipe);
  ebbackup::BackupEngine::InitRepo(repo_lz4);

  ebbackup::BackupOptions seq_opts{};
  ebbackup::BackupOptions pipe_opts{};
  pipe_opts.use_pipeline = true;
  ebbackup::BackupOptions lz4_opts{};
  lz4_opts.use_lz4 = true;
  lz4_opts.use_pipeline = true;

  const double seq_sec = RunBackup(repo_seq, source, seq_opts);
  const double pipe_sec = RunBackup(repo_pipe, source, pipe_opts);
  const double lz4_sec = RunBackup(repo_lz4, source, lz4_opts);

  const double mb = static_cast<double>(file_size) / (1024.0 * 1024.0);
  const double seq_mbps = seq_sec > 0 ? mb / seq_sec : 0.0;
  const double pipe_mbps = pipe_sec > 0 ? mb / pipe_sec : 0.0;
  const double lz4_mbps = lz4_sec > 0 ? mb / lz4_sec : 0.0;
  const double ratio = seq_mbps > 0 ? pipe_mbps / seq_mbps : 0.0;

  std::printf(
      "bench pipeline: file_mb=%.2f sequential=%.2f MB/s pipeline=%.2f MB/s "
      "lz4_pipeline=%.2f MB/s pipeline_ratio=%.2f\n",
      mb, seq_mbps, pipe_mbps, lz4_mbps, ratio);

  if (ratio > 0 && ratio < 0.90) {
    std::fprintf(stderr, "pipeline throughput below 90%% of sequential\n");
    return 1;
  }
  return 0;
}
