#include <gtest/gtest.h>

#include <cstdlib>
#include <vector>

#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/rolling_checksum.h"
#include "test_util.h"

namespace ebbackup {
namespace {

void PopulateRolling(const uint8_t* data, size_t len, CfiIndex* cfi) {
  for (auto& a : cfi->anchors) {
    if (a.offset + a.length <= len) {
      a.rolling_checksum = RollingChecksum(data + a.offset, a.length);
    }
  }
}

double MeasureReusePct(const std::vector<uint8_t>& data, size_t delta_offset) {
  std::vector<uint8_t> mutated = data;
  if (delta_offset < mutated.size()) {
    mutated[delta_offset] ^= 0x3C;
  }

  EbHcrboChunker hcrbo;
  CfiIndex cfi;
  std::vector<ChunkDescriptor> full;
  if (!hcrbo.ChunkFull(mutated.data(), mutated.size(), &full, &cfi, nullptr).ok()) {
    return 0.0;
  }
  PopulateRolling(mutated.data(), mutated.size(), &cfi);

  std::vector<ChunkDescriptor> incr;
  CfiIndex cfi_out;
  EbHcrboStats stats{};
  if (!hcrbo.ChunkIncremental(mutated.data(), mutated.size(), cfi, &incr,
                              &cfi_out, &stats)
           .ok()) {
    return 0.0;
  }
  if (full.empty()) return 0.0;
  return 100.0 * static_cast<double>(stats.chunks_reused_from_cfi) /
         static_cast<double>(full.size());
}

TEST(BenchReuseGateTest, L1SyntheticReuseAtLeast90Pct) {
#if defined(_WIN32)
  _putenv_s("EBBACKUP_DIGEST_THREADS", "4");
#else
  setenv("EBBACKUP_DIGEST_THREADS", "4", 1);
#endif
  constexpr size_t kSize = 64 * 1024 * 1024;
  constexpr size_t kDeltaOffset = 5 * 1024 * 1024;
  const std::string raw = test::MakeSyntheticData(kSize, 7);
  const std::vector<uint8_t> data(raw.begin(), raw.end());

  double min_reuse = 100.0;
  for (int run = 0; run < 3; ++run) {
    min_reuse = std::min(min_reuse, MeasureReusePct(data, kDeltaOffset));
  }
  EXPECT_GE(min_reuse, 90.0) << "reuse_pct must not regress below CI floor";
}

}  // namespace
}  // namespace ebbackup
