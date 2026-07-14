#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/gt_cdc.h"
#include "ebbackup/chunk/gt_cdc_internal.h"
#include "ebbackup/chunk/gt_cdc_streaming.h"
#include "test_util.h"

namespace ebbackup {
namespace {

GtCdcConfig MakeTwoFGearTestConfig(size_t file_size) {
  GtCdcConfig cfg = GtCdcConfigForFileSize(
      file_size, ChunkProfileMode::kAuto, DigestAlgo::kLegacy,
      GtCdcKernel::kTwoFGear);
  cfg.table_seed = 0x12345678u;
  cfg.nc_level = 2;
  gtcdc_internal::InitGearTableForConfig(&cfg);
  return cfg;
}

double MeanChunkLength(const std::vector<uint32_t>& lengths) {
  if (lengths.empty()) return 0.0;
  double mean = 0.0;
  for (uint32_t len : lengths) mean += static_cast<double>(len);
  return mean / static_cast<double>(lengths.size());
}

TEST(GtCdc2FTest, HitRatePositive) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 37);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig cfg = MakeTwoFGearTestConfig(kFileSize);
  GtCdcSlice chunker(cfg);
  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &offsets, &lengths).ok());
  ASSERT_GT(lengths.size(), 3u);

  size_t hash_hits = 0;
  for (uint32_t len : lengths) {
    if (len < cfg.max_size) ++hash_hits;
  }
  EXPECT_GT(hash_hits, 0u) << "2F-Gear should produce non-max hash-based cuts";
}

TEST(GtCdc2FTest, MeanLengthNearAvg) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 41);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig cfg = MakeTwoFGearTestConfig(kFileSize);
  GtCdcSlice chunker(cfg);
  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &offsets, &lengths).ok());
  const double mean = MeanChunkLength(lengths);
  EXPECT_GE(mean, static_cast<double>(cfg.avg_size) / 2.0);
  EXPECT_LE(mean, static_cast<double>(cfg.avg_size) * 2.0);
}

TEST(GtCdc2FTest, DiffersFromGear) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 55);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig gear_cfg = GtCdcConfigForFileSize(
      kFileSize, ChunkProfileMode::kAuto, DigestAlgo::kLegacy,
      GtCdcKernel::kGear);
  gtcdc_internal::InitGearTableForConfig(&gear_cfg);
  GtCdcSlice gear(gear_cfg);
  GtCdcSlice two_f(MakeTwoFGearTestConfig(kFileSize));
  std::vector<size_t> gear_off;
  std::vector<uint32_t> gear_len;
  std::vector<size_t> tf_off;
  std::vector<uint32_t> tf_len;
  ASSERT_TRUE(gear.ChunkCuts(bytes, kFileSize, &gear_off, &gear_len).ok());
  ASSERT_TRUE(two_f.ChunkCuts(bytes, kFileSize, &tf_off, &tf_len).ok());
  EXPECT_FALSE(gear_off == tf_off && gear_len == tf_len);
}

TEST(GtCdc2FTest, DeterministicGolden) {
  constexpr size_t kFileSize = 2 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 99);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig cfg = MakeTwoFGearTestConfig(kFileSize);
  GtCdcSlice a(cfg);
  GtCdcSlice b(cfg);
  std::vector<size_t> off_a;
  std::vector<uint32_t> len_a;
  std::vector<size_t> off_b;
  std::vector<uint32_t> len_b;
  ASSERT_TRUE(a.ChunkCuts(bytes, kFileSize, &off_a, &len_a).ok());
  ASSERT_TRUE(b.ChunkCuts(bytes, kFileSize, &off_b, &len_b).ok());
  EXPECT_EQ(off_a, off_b);
  EXPECT_EQ(len_a, len_b);
}

TEST(GtCdc2FTest, StreamingMatchesFullFile) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 93);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig cfg = MakeTwoFGearTestConfig(kFileSize);
  GtCdcSlice chunker(cfg);
  std::vector<size_t> full_off;
  std::vector<uint32_t> full_len;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &full_off, &full_len).ok());

  GtCdcStreamState state{};
  GtCdcStreamInit(&state, cfg);
  state.digest_base = bytes;

  std::vector<size_t> stream_off;
  std::vector<uint32_t> stream_len;
  std::vector<ChunkDescriptor> batch;
  ASSERT_TRUE(GtCdcStreamFeed(&state, bytes, kFileSize, true, &batch).ok());
  for (const auto& d : batch) {
    stream_off.push_back(d.offset);
    stream_len.push_back(d.length);
  }

  ASSERT_EQ(full_off.size(), stream_off.size());
  for (size_t i = 0; i < full_off.size(); ++i) {
    EXPECT_EQ(full_off[i], stream_off[i]) << " at index " << i;
    EXPECT_EQ(full_len[i], stream_len[i]) << " at index " << i;
  }
}

TEST(GtCdc2FTest, StreamingMatchesMultiFeed512K) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  constexpr size_t kFeed = 512 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 93);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig cfg = MakeTwoFGearTestConfig(kFileSize);
  GtCdcSlice chunker(cfg);
  std::vector<size_t> full_off;
  std::vector<uint32_t> full_len;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &full_off, &full_len).ok());

  GtCdcStreamState state{};
  GtCdcStreamInit(&state, cfg);
  state.digest_base = bytes;

  std::vector<size_t> stream_off;
  std::vector<uint32_t> stream_len;
  for (size_t feed_off = 0; feed_off < kFileSize; feed_off += kFeed) {
    const size_t n = std::min(kFeed, kFileSize - feed_off);
    const bool last = (feed_off + n >= kFileSize);
    std::vector<ChunkDescriptor> batch;
    ASSERT_TRUE(GtCdcStreamFeed(&state, bytes + feed_off, n, last, &batch).ok());
    for (const auto& d : batch) {
      stream_off.push_back(d.offset);
      stream_len.push_back(d.length);
    }
  }

  ASSERT_EQ(full_off.size(), stream_off.size());
  for (size_t i = 0; i < full_off.size(); ++i) {
    EXPECT_EQ(full_off[i], stream_off[i]);
    EXPECT_EQ(full_len[i], stream_len[i]);
  }
}

TEST(GtCdc2FTest, StreamingRecordsScanProbes) {
#if defined(_WIN32)
  _putenv_s("EBBACKUP_PIPELINE_PROFILE", "1");
#else
  setenv("EBBACKUP_PIPELINE_PROFILE", "1", 1);
#endif
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 93);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig cfg = MakeTwoFGearTestConfig(kFileSize);
  GtCdcStreamState state{};
  GtCdcStreamInit(&state, cfg);
  state.digest_base = bytes;
  std::vector<ChunkDescriptor> batch;
  ASSERT_TRUE(GtCdcStreamFeed(&state, bytes, kFileSize, true, &batch).ok());
  EXPECT_GT(state.profile.gtcdc_scan_probes, 1000u);
  EXPECT_GT(state.profile.gtcdc_scan_ns, 0u);
}

TEST(GtCdc2FTest, V6RepoFlag) {
  const std::string repo = test::TempDir("gtcdc_v6_flag");
  ASSERT_TRUE(test::InitGtCdcV6Repo(repo).ok());
  EXPECT_TRUE(test::RepoHasGtCdcTwoFGear(repo));
  EXPECT_FALSE(test::RepoHasGtCdcAnGear(repo));
}

}  // namespace
}  // namespace ebbackup
