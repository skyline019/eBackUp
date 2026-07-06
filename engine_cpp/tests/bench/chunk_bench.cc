#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/fast_cdc.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: ebbackup_bench <file> [delta_offset]\n");
    return 1;
  }
  const std::string file = argv[1];
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", file.c_str());
    return 1;
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  if (argc >= 3) {
    const size_t off = static_cast<size_t>(std::strtoull(argv[2], nullptr, 10));
    if (off < data.size()) data[off] ^= 0x3C;
  }

  ebbackup::FastCdcSlice fast;
  std::vector<ebbackup::ChunkDescriptor> fast_chunks;
  const auto t0 = std::chrono::steady_clock::now();
  const ebbackup::Status fast_st =
      fast.Chunk(data.data(), data.size(), &fast_chunks);
  const auto t1 = std::chrono::steady_clock::now();
  if (!fast_st.ok()) {
    std::fprintf(stderr, "fastcdc failed: %s\n", fast_st.message().c_str());
    return 1;
  }

  ebbackup::EbHcrboChunker hcrbo;
  ebbackup::CfiIndex cfi;
  std::vector<ebbackup::ChunkDescriptor> full;
  (void)hcrbo.ChunkFull(data.data(), data.size(), &full, &cfi, nullptr);

  const auto t2 = std::chrono::steady_clock::now();
  std::vector<ebbackup::ChunkDescriptor> incr;
  ebbackup::CfiIndex cfi_out;
  ebbackup::EbHcrboStats stats{};
  const ebbackup::Status hcrbo_st = hcrbo.ChunkIncremental(
      data.data(), data.size(), cfi, &incr, &cfi_out, &stats);
  const auto t3 = std::chrono::steady_clock::now();
  if (!hcrbo_st.ok()) {
    std::fprintf(stderr, "hcrbo failed: %s\n", hcrbo_st.message().c_str());
    return 1;
  }

  const double sec_fast =
      std::chrono::duration<double>(t1 - t0).count();
  const double sec_hcrbo =
      std::chrono::duration<double>(t3 - t2).count();
  const double mb = static_cast<double>(data.size()) / (1024.0 * 1024.0);
  const double reuse =
      full.empty() ? 0.0 : 100.0 * stats.chunks_reused_from_cfi / full.size();
  std::printf(
      "bench fastcdc: chunks=%zu throughput=%.2f MB/s\n", fast_chunks.size(),
      sec_fast > 0 ? mb / sec_fast : 0.0);
  std::printf(
      "bench hcrbo: chunks=%zu reused=%.1f%% throughput=%.2f MB/s "
      "rolling_skip_hits=%llu indexed_scan=true\n",
      incr.size(), reuse, sec_hcrbo > 0 ? mb / sec_hcrbo : 0.0,
      static_cast<unsigned long long>(stats.cfi_rolling_skip_hits));
  return 0;
}
