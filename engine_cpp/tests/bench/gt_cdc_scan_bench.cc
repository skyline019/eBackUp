#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>
#include <vector>

#include "ebbackup/bench/throughput.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/chunk/gt_cdc.h"

namespace {

std::string MakeSyntheticData(size_t size, uint8_t seed) {
  std::string data(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<char>((seed + i * 17 + (i >> 8)) & 0xFF);
  }
  return data;
}

template <typename Fn>
double BenchCutsMs(Fn&& fn) {
  const auto t0 = std::chrono::steady_clock::now();
  fn();
  const auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

double Percentile(std::vector<double> values, double pct) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const size_t idx =
      std::min(values.size() - 1,
               static_cast<size_t>(pct * static_cast<double>(values.size() - 1)));
  return values[idx];
}

}  // namespace

int main(int argc, char** argv) {
  bool enforce = false;
  int runs = 5;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--enforce") {
      enforce = true;
    } else if (std::string(argv[i]) == "--runs" && i + 1 < argc) {
      runs = std::max(1, std::atoi(argv[i + 1]));
      ++i;
    }
  }

  constexpr size_t kFileSize = 256u * 1024u * 1024u;
  const std::string raw = MakeSyntheticData(kFileSize, 43);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(raw.data());

  ebbackup::FastCdcSlice fast;
  ebbackup::GtCdcSlice gtcdc;
  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;

  for (int w = 0; w < 2; ++w) {
    (void)fast.ChunkCuts(bytes, kFileSize, &offsets, &lengths);
    offsets.clear();
    lengths.clear();
    (void)gtcdc.ChunkCuts(bytes, kFileSize, &offsets, &lengths);
    offsets.clear();
    lengths.clear();
  }

  const double fast_ms = BenchCutsMs([&] {
    offsets.clear();
    lengths.clear();
    (void)fast.ChunkCuts(bytes, kFileSize, &offsets, &lengths);
  });

  const double gtcdc_scalar_ms = BenchCutsMs([&] {
    offsets.clear();
    lengths.clear();
    (void)gtcdc.ChunkCutsScalar(bytes, kFileSize, &offsets, &lengths);
  });

  std::vector<double> tensor_ms;
  std::vector<double> tensor_ratios;
  tensor_ms.reserve(static_cast<size_t>(runs));
  tensor_ratios.reserve(static_cast<size_t>(runs));

  for (int r = 0; r < runs; ++r) {
    const double ms = BenchCutsMs([&] {
      offsets.clear();
      lengths.clear();
      (void)gtcdc.ChunkCuts(bytes, kFileSize, &offsets, &lengths);
    });
    tensor_ms.push_back(ms);
  }

  const double gtcdc_tensor_ms =
      std::accumulate(tensor_ms.begin(), tensor_ms.end(), 0.0) /
      static_cast<double>(tensor_ms.size());

  const double fast_MBps = ebbackup::bench::ThroughputMBps(kFileSize, fast_ms / 1000.0);
  const double gtcdc_scalar_MBps =
      ebbackup::bench::ThroughputMBps(kFileSize, gtcdc_scalar_ms / 1000.0);

  for (double ms : tensor_ms) {
    const double tensor_MBps =
        ebbackup::bench::ThroughputMBps(kFileSize, ms / 1000.0);
    tensor_ratios.push_back(gtcdc_scalar_MBps > 0 ? tensor_MBps / gtcdc_scalar_MBps
                                                  : 0.0);
  }

  const double gtcdc_tensor_MBps =
      ebbackup::bench::ThroughputMBps(kFileSize, gtcdc_tensor_ms / 1000.0);
  const double tensor_vs_scalar =
      gtcdc_scalar_MBps > 0 ? gtcdc_tensor_MBps / gtcdc_scalar_MBps : 0.0;
  const double ratio_p50 = Percentile(tensor_ratios, 0.50);
  const double ratio_min =
      tensor_ratios.empty() ? 0.0
                            : *std::min_element(tensor_ratios.begin(), tensor_ratios.end());
  const double ratio_max =
      tensor_ratios.empty() ? 0.0
                            : *std::max_element(tensor_ratios.begin(), tensor_ratios.end());

  std::printf("gtcdc_scan_bench 256MB (runs=%d):\n", runs);
  std::printf("  fastcdc_ChunkCuts_MBps=%.2f (%.1f ms)\n", fast_MBps, fast_ms);
  std::printf("  gtcdc_scalar_MBps=%.2f (%.1f ms)\n", gtcdc_scalar_MBps,
              gtcdc_scalar_ms);
  std::printf("  gtcdc_tensor_MBps=%.2f (mean %.1f ms)\n", gtcdc_tensor_MBps,
              gtcdc_tensor_ms);
  std::printf("  tensor_vs_scalar=%.3f p50=%.3f min=%.3f max=%.3f\n",
              tensor_vs_scalar, ratio_p50, ratio_min, ratio_max);

  if (tensor_vs_scalar < 1.0) {
    std::fprintf(stderr, "warning: tensor scan slower than scalar (%.3f)\n",
                 tensor_vs_scalar);
  }
  if (enforce && ratio_p50 + 1e-6 < 1.30) {
    std::fprintf(stderr,
                 "enforce failed: tensor_vs_scalar p50 %.3f below target 1.30\n",
                 ratio_p50);
    return 1;
  }
  return 0;
}
