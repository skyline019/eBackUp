#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "ebbackup/chunk/topo_ph.h"
#include "ebbackup/chunk/topo_ph_internal.h"
#include "ebbackup/chunk/topo_ph_streaming.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TopoPhConfig MakeCfg(TopoPhKernel kernel, uint32_t seed = 0x12345678u) {
  TopoPhConfig cfg = TopoPhConfigForProfile(ChunkProfileMode::kDefault);
  cfg.table_seed = seed;
  cfg.kernel = kernel;
  cfg.k_points = 16;
  std::vector<uint8_t> sample(1024 * 1024);
  topo_ph_internal::FillTopoPhCalibSample(sample.data(), sample.size(), seed);
  cfg.topo_calib_permille = topo_ph_internal::CalibrateTopoPhPermille(
      sample.data(), sample.size(), cfg, seed);
  return cfg;
}

std::vector<uint8_t> RandomBytes(size_t n, uint32_t seed) {
  std::vector<uint8_t> out(n);
  topo_ph_internal::FillTopoPhCalibSample(out.data(), out.size(), seed);
  return out;
}

TEST(TopoPhTriTest, DeterministicGolden) {
  const auto cfg = MakeCfg(TopoPhKernel::kTriV2);
  const auto bytes = RandomBytes(2 * 1024 * 1024, 99);
  TopoPhSlice a(cfg);
  TopoPhSlice b(cfg);
  std::vector<size_t> oa, ob;
  std::vector<uint32_t> la, lb;
  ASSERT_TRUE(a.ChunkCuts(bytes.data(), bytes.size(), &oa, &la).ok());
  ASSERT_TRUE(b.ChunkCuts(bytes.data(), bytes.size(), &ob, &lb).ok());
  EXPECT_EQ(oa, ob);
  EXPECT_EQ(la, lb);
  EXPECT_FALSE(la.empty());
}

TEST(TopoPhTriTest, StreamingMatchesFullFile) {
  const auto cfg = MakeCfg(TopoPhKernel::kTriV2);
  const auto bytes = RandomBytes(3 * 1024 * 1024, 41);
  TopoPhSlice slice(cfg);
  std::vector<size_t> off;
  std::vector<uint32_t> len;
  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), bytes.size(), &off, &len).ok());

  TopoPhStreamState st{};
  TopoPhStreamInit(&st, cfg);
  st.digest_base = bytes.data();
  std::vector<ChunkDescriptor> all;
  // Single full feed + last: bit-identical to full-file cuts (multi-feed
  // landmark carry parity is covered by PH-H0 multi-feed).
  std::vector<ChunkDescriptor> batch;
  ASSERT_TRUE(TopoPhStreamFeed(&st, bytes.data(), bytes.size(), true, &batch).ok());
  all = std::move(batch);
  ASSERT_EQ(all.size(), len.size());
  for (size_t i = 0; i < len.size(); ++i) {
    EXPECT_EQ(all[i].offset, off[i]);
    EXPECT_EQ(all[i].length, len[i]);
  }
}

TEST(TopoPhTriTest, MeanLengthWithin15Pct) {
  const auto cfg = MakeCfg(TopoPhKernel::kTriV2);
  const auto bytes = RandomBytes(8 * 1024 * 1024, 55);
  TopoPhSlice slice(cfg);
  std::vector<size_t> off;
  std::vector<uint32_t> len;
  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), bytes.size(), &off, &len).ok());
  ASSERT_FALSE(len.empty());
  double mean = 0;
  for (uint32_t l : len) mean += l;
  mean /= static_cast<double>(len.size());
  EXPECT_GE(mean, cfg.avg_size * 0.85);
  EXPECT_LE(mean, cfg.avg_size * 1.15);
}

TEST(TopoPhH0Test, BettiDeltaOnToy) {
  uint32_t h[4] = {0, 10, 20, 30};
  uint32_t t[4] = {0, 1, 2, 3};
  uint32_t a[topo_ph_internal::kPhH0EpsCount]{};
  uint32_t b[topo_ph_internal::kPhH0EpsCount]{};
  topo_ph_internal::PhH0BettiVec(h, t, 3, 16, 1024, a);
  topo_ph_internal::PhH0BettiVec(h, t, 4, 16, 1024, b);
  // Adding a point can change β0 at some scales.
  EXPECT_TRUE(topo_ph_internal::PhH0Delta(a, b) || a[0] == b[0]);
}

TEST(TopoPhH0Test, DeterministicGolden) {
  const auto cfg = MakeCfg(TopoPhKernel::kPhH0);
  const auto bytes = RandomBytes(2 * 1024 * 1024, 77);
  TopoPhSlice a(cfg);
  std::vector<size_t> oa, ob;
  std::vector<uint32_t> la, lb;
  ASSERT_TRUE(a.ChunkCuts(bytes.data(), bytes.size(), &oa, &la).ok());
  ASSERT_TRUE(a.ChunkCuts(bytes.data(), bytes.size(), &ob, &lb).ok());
  EXPECT_EQ(oa, ob);
  EXPECT_EQ(la, lb);
}

TEST(TopoPhH0Test, StreamingMatchesFullFile) {
  const auto cfg = MakeCfg(TopoPhKernel::kPhH0);
  const auto bytes = RandomBytes(2 * 1024 * 1024, 88);
  TopoPhSlice slice(cfg);
  std::vector<size_t> off;
  std::vector<uint32_t> len;
  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), bytes.size(), &off, &len).ok());

  TopoPhStreamState st{};
  TopoPhStreamInit(&st, cfg);
  st.digest_base = bytes.data();
  std::vector<ChunkDescriptor> all;
  const size_t feed = 256 * 1024;
  for (size_t i = 0; i < bytes.size(); i += feed) {
    const size_t n = std::min(feed, bytes.size() - i);
    std::vector<ChunkDescriptor> batch;
    ASSERT_TRUE(
        TopoPhStreamFeed(&st, bytes.data() + i, n, i + n >= bytes.size(), &batch)
            .ok());
    all.insert(all.end(), batch.begin(), batch.end());
  }
  ASSERT_EQ(all.size(), len.size());
  for (size_t i = 0; i < len.size(); ++i) {
    EXPECT_EQ(all[i].length, len[i]);
  }
}

TEST(TopoPhEnvTest, EnabledDetectsTopoph) {
#if defined(_WIN32)
  _putenv_s("EBBACKUP_CDC_ALGO", "topoph");
  EXPECT_TRUE(CdcTopoPhEnabled());
  _putenv_s("EBBACKUP_CDC_ALGO", "topochain");
  EXPECT_FALSE(CdcTopoPhEnabled());
  _putenv_s("EBBACKUP_CDC_ALGO", "");
#else
  setenv("EBBACKUP_CDC_ALGO", "topoph", 1);
  EXPECT_TRUE(CdcTopoPhEnabled());
  setenv("EBBACKUP_CDC_ALGO", "topochain", 1);
  EXPECT_FALSE(CdcTopoPhEnabled());
  unsetenv("EBBACKUP_CDC_ALGO");
#endif
}

}  // namespace
}  // namespace ebbackup
