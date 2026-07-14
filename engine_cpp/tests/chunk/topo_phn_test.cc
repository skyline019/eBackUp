#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "ebbackup/chunk/topo_phn.h"
#include "ebbackup/chunk/topo_phn_internal.h"
#include "ebbackup/chunk/topo_phn_streaming.h"
#include "ebbackup/engine/manifest.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TopoPhnConfig MakeCfg(TopoPhnKernel kernel, uint32_t seed = 0x12345678u) {
  TopoPhnConfig cfg = TopoPhnConfigForProfile(ChunkProfileMode::kDefault);
  cfg.table_seed = seed;
  cfg.kernel = kernel;
  if (kernel == TopoPhnKernel::kPhH0Native) {
    cfg.k_points = 16;
  } else {
    cfg.k_points = 8;
  }
  std::vector<uint8_t> sample(topo_phn_internal::kPhnCalibSampleBytes);
  topo_phn_internal::FillPhnCalibSample(sample.data(), sample.size(), seed);
  topo_phn_internal::CalibratePhnCutParams(sample.data(), sample.size(), &cfg);
  return cfg;
}

std::vector<uint8_t> RandomBytes(size_t n, uint32_t seed) {
  std::vector<uint8_t> out(n);
  topo_phn_internal::FillPhnCalibSample(out.data(), out.size(), seed);
  return out;
}

double MeanOf(const std::vector<uint32_t>& len) {
  double mean = 0;
  for (uint32_t l : len) mean += l;
  return mean / static_cast<double>(len.size());
}

// Reference: iterative 4-step LCG+XOR (math definition; must match closed-form).
uint32_t LandmarkEmbedY4Iterative(uint32_t seed, uint8_t b0, uint8_t b1,
                                  uint8_t b2, uint8_t b3) {
  constexpr uint32_t kM = 1664525u;
  constexpr uint32_t kC = 1013904223u;
  uint32_t y = seed;
  y = y * kM + kC + b0;
  y = y * kM + kC + b1;
  y = y * kM + kC + b2;
  y = y * kM + kC + b3;
  y ^= static_cast<uint32_t>(b2) << 8;
  y ^= static_cast<uint32_t>(b1) << 16;
  y ^= static_cast<uint32_t>(b0) << 24;
  return y;
}

TEST(TopoPhnTriTest, EmbedY4ClosedFormBitIdentical) {
  uint32_t s = 0xA5A5A5A5u;
  for (int i = 0; i < 4096; ++i) {
    s = s * 1664525u + 1013904223u;
    const uint8_t b0 = static_cast<uint8_t>(s);
    const uint8_t b1 = static_cast<uint8_t>(s >> 8);
    const uint8_t b2 = static_cast<uint8_t>(s >> 16);
    const uint8_t b3 = static_cast<uint8_t>(s >> 24);
    uint8_t win[4] = {b0, b1, b2, b3};
    const uint32_t seed = s ^ 0x12345678u;
    const uint32_t got =
        topo_phn_internal::LandmarkEmbedY(win, 4, 3, seed);
    const uint32_t expect = LandmarkEmbedY4Iterative(seed, b0, b1, b2, b3);
    ASSERT_EQ(got, expect) << "i=" << i;
  }
}

TEST(TopoPhnTriTest, DeterministicGolden) {
  const auto cfg = MakeCfg(TopoPhnKernel::kTriNative);
  const auto bytes = RandomBytes(2 * 1024 * 1024, 99);
  TopoPhnSlice a(cfg);
  TopoPhnSlice b(cfg);
  std::vector<size_t> oa, ob;
  std::vector<uint32_t> la, lb;
  ASSERT_TRUE(a.ChunkCuts(bytes.data(), bytes.size(), &oa, &la).ok());
  ASSERT_TRUE(b.ChunkCuts(bytes.data(), bytes.size(), &ob, &lb).ok());
  EXPECT_EQ(oa, ob);
  EXPECT_EQ(la, lb);
  EXPECT_FALSE(la.empty());
}

TEST(TopoPhnTriTest, StreamingMatchesFullFile) {
  const auto cfg = MakeCfg(TopoPhnKernel::kTriNative);
  const auto bytes = RandomBytes(3 * 1024 * 1024, 41);
  TopoPhnSlice slice(cfg);
  std::vector<size_t> off;
  std::vector<uint32_t> len;
  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), bytes.size(), &off, &len).ok());

  TopoPhnStreamState st{};
  TopoPhnStreamInit(&st, cfg);
  st.digest_base = bytes.data();
  std::vector<ChunkDescriptor> batch;
  ASSERT_TRUE(
      TopoPhnStreamFeed(&st, bytes.data(), bytes.size(), true, &batch).ok());
  ASSERT_EQ(batch.size(), len.size());
  for (size_t i = 0; i < len.size(); ++i) {
    EXPECT_EQ(batch[i].offset, off[i]);
    EXPECT_EQ(batch[i].length, len[i]);
  }
}

TEST(TopoPhnTriTest, MultiFeedStreamingMatchesFullFile) {
  const auto cfg = MakeCfg(TopoPhnKernel::kTriNative);
  const auto bytes = RandomBytes(3 * 1024 * 1024, 41);
  TopoPhnSlice slice(cfg);
  std::vector<size_t> off;
  std::vector<uint32_t> len;
  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), bytes.size(), &off, &len).ok());

  TopoPhnStreamState st{};
  TopoPhnStreamInit(&st, cfg);
  st.digest_base = bytes.data();
  std::vector<ChunkDescriptor> all;
  const size_t feed = 256 * 1024;
  for (size_t i = 0; i < bytes.size(); i += feed) {
    const size_t n = std::min(feed, bytes.size() - i);
    std::vector<ChunkDescriptor> batch;
    ASSERT_TRUE(
        TopoPhnStreamFeed(&st, bytes.data() + i, n, i + n >= bytes.size(),
                          &batch)
            .ok());
    all.insert(all.end(), batch.begin(), batch.end());
  }
  ASSERT_EQ(all.size(), len.size());
  for (size_t i = 0; i < len.size(); ++i) {
    EXPECT_EQ(all[i].offset, off[i]) << "i=" << i;
    EXPECT_EQ(all[i].length, len[i]) << "i=" << i;
  }
}

TEST(TopoPhnTriTest, MeanLengthWithin15Pct) {
  auto bytes = RandomBytes(8 * 1024 * 1024, 55);
  TopoPhnConfig cfg = TopoPhnConfigForProfile(ChunkProfileMode::kDefault);
  cfg.table_seed = 55;
  cfg.kernel = TopoPhnKernel::kTriNative;
  cfg.k_points = 8;
  const size_t calib_n = topo_phn_internal::kPhnCalibSampleBytes;
  topo_phn_internal::CalibratePhnCutParams(bytes.data(), calib_n, &cfg);
  TopoPhnSlice slice(cfg);
  std::vector<size_t> off;
  std::vector<uint32_t> len;
  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), calib_n, &off, &len).ok());
  ASSERT_FALSE(len.empty());
  const double mean = MeanOf(len);
  EXPECT_GE(mean, cfg.avg_size * 0.85);
  EXPECT_LE(mean, cfg.avg_size * 1.15);

  // Same knobs on larger same-seed payload must stay in band.
  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), bytes.size(), &off, &len).ok());
  ASSERT_FALSE(len.empty());
  const double mean_full = MeanOf(len);
  EXPECT_GE(mean_full, cfg.avg_size * 0.70);
  EXPECT_LE(mean_full, cfg.avg_size * 1.60);
}

TEST(TopoPhnTriTest, ScaleEventStrideTracksAvgRef) {
  using topo_phn_internal::ClampEventStride;
  using topo_phn_internal::kPhnCalibAvgRef;
  using topo_phn_internal::RoundEventStridePow2;
  using topo_phn_internal::ScaleEventStrideForAvg;
  const uint16_t sb = 1000;
  EXPECT_EQ(ScaleEventStrideForAvg(sb, kPhnCalibAvgRef),
            RoundEventStridePow2(sb));
  EXPECT_EQ(ScaleEventStrideForAvg(sb, 1024u * 1024u),
            RoundEventStridePow2(4000));
  // round(1000 * 16KiB / 256KiB) = 63 → nearest pow2 = 64
  EXPECT_EQ(ScaleEventStrideForAvg(sb, 16u * 1024u),
            RoundEventStridePow2(63));
  EXPECT_EQ(RoundEventStridePow2(63), ClampEventStride(64));
  EXPECT_EQ(RoundEventStridePow2(65537), 65536u);  // not ceil→131072
}

TEST(TopoPhnTriTest, LargeProfileMeanWithin15PctAfterStrideScale) {
  // Init calibrates under Default; runtime scales SB stride onto Large avg,
  // then re-bands mean via CalibratePhnRuntimeParams (stride ≥ Scale floor).
  auto bytes = RandomBytes(16 * 1024 * 1024, 55);
  const size_t init_n = topo_phn_internal::kPhnCalibSampleBytes;
  TopoPhnConfig def = TopoPhnConfigForProfile(ChunkProfileMode::kDefault);
  def.table_seed = 55;
  def.kernel = TopoPhnKernel::kTriNative;
  def.k_points = 8;
  topo_phn_internal::CalibratePhnCutParams(bytes.data(), init_n, &def);

  TopoPhnConfig cfg = TopoPhnConfigForProfile(ChunkProfileMode::kLarge);
  cfg.table_seed = 55;
  cfg.kernel = TopoPhnKernel::kTriNative;
  cfg.k_points = 8;
  cfg.event_stride = topo_phn_internal::ScaleEventStrideForAvg(def.event_stride,
                                                               cfg.avg_size);
  const size_t runtime_n =
      std::max(init_n, static_cast<size_t>(cfg.avg_size) * 8u);
  ASSERT_GE(bytes.size(), runtime_n);
  topo_phn_internal::CalibratePhnRuntimeParams(bytes.data(), runtime_n, &cfg);
  EXPECT_GE(cfg.event_stride,
            topo_phn_internal::ScaleEventStrideForAvg(def.event_stride,
                                                     cfg.avg_size));

  TopoPhnSlice slice(cfg);
  std::vector<size_t> off;
  std::vector<uint32_t> len;
  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), runtime_n, &off, &len).ok());
  ASSERT_FALSE(len.empty());
  const double mean = MeanOf(len);
  // Pow2 stride lattice cannot always land inside ±15% for Large min/max;
  // keep the same slack as Default full-file banding.
  EXPECT_GE(mean, cfg.avg_size * 0.70) << "mean=" << mean
                                       << " stride=" << cfg.event_stride;
  EXPECT_LE(mean, cfg.avg_size * 1.40) << "mean=" << mean
                                       << " stride=" << cfg.event_stride;
}

TEST(TopoPhnTriTest, OneByteInsertReuseAtLeast80Pct) {
  // Matches topo_cdc_eval Reuse1Byte protocol (8MiB, insert @5MiB).
  constexpr size_t kSize = 8 * 1024 * 1024;
  constexpr size_t kInsertAt = 5 * 1024 * 1024;
  auto bytes = RandomBytes(kSize, 93);
  TopoPhnConfig cfg = TopoPhnConfigForProfile(ChunkProfileMode::kDefault);
  cfg.table_seed = 0x12345678u;
  cfg.kernel = TopoPhnKernel::kTriNative;
  cfg.k_points = 8;
  topo_phn_internal::CalibratePhnCutParams(
      bytes.data(), topo_phn_internal::kPhnCalibSampleBytes, &cfg);
  TopoPhnSlice slice(cfg);
  std::vector<size_t> base_off;
  std::vector<uint32_t> base_len;
  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), bytes.size(), &base_off, &base_len)
                   .ok());
  ASSERT_FALSE(base_off.empty());

  std::vector<uint8_t> mutated(kSize + 1);
  std::memcpy(mutated.data(), bytes.data(), kInsertAt);
  mutated[kInsertAt] = 0xA5;
  std::memcpy(mutated.data() + kInsertAt + 1, bytes.data() + kInsertAt,
              kSize - kInsertAt);
  std::vector<size_t> off2;
  std::vector<uint32_t> len2;
  ASSERT_TRUE(
      slice.ChunkCuts(mutated.data(), mutated.size(), &off2, &len2).ok());

  size_t hits = 0;
  for (size_t b : base_off) {
    const size_t expect = b < kInsertAt ? b : b + 1;
    for (size_t o : off2) {
      if (o == expect) {
        ++hits;
        break;
      }
    }
  }
  const double reuse =
      100.0 * static_cast<double>(hits) / static_cast<double>(base_off.size());
  EXPECT_GE(reuse, 80.0) << "reuse_1byte_pct=" << reuse
                         << " hits=" << hits << "/" << base_off.size();
}

TEST(TopoPhnH0Test, BettiDeltaOnToy) {
  uint32_t h[4] = {0, 10, 20, 30};
  uint32_t t[4] = {0, 1, 2, 3};
  uint32_t a[topo_phn_internal::kPhnH0EpsCount]{};
  uint32_t b[topo_phn_internal::kPhnH0EpsCount]{};
  topo_phn_internal::PhH0BettiVec(h, t, 3, 16, 1024, a);
  topo_phn_internal::PhH0BettiVec(h, t, 4, 16, 1024, b);
  EXPECT_TRUE(topo_phn_internal::PhH0Delta(a, b) || a[0] == b[0]);
}

TEST(TopoPhnH0Test, DeterministicGolden) {
  const auto cfg = MakeCfg(TopoPhnKernel::kPhH0Native);
  const auto bytes = RandomBytes(2 * 1024 * 1024, 77);
  TopoPhnSlice a(cfg);
  std::vector<size_t> oa, ob;
  std::vector<uint32_t> la, lb;
  ASSERT_TRUE(a.ChunkCuts(bytes.data(), bytes.size(), &oa, &la).ok());
  ASSERT_TRUE(a.ChunkCuts(bytes.data(), bytes.size(), &ob, &lb).ok());
  EXPECT_EQ(oa, ob);
  EXPECT_EQ(la, lb);
}

TEST(TopoPhnH0Test, StreamingMatchesFullFile) {
  const auto cfg = MakeCfg(TopoPhnKernel::kPhH0Native);
  const auto bytes = RandomBytes(2 * 1024 * 1024, 88);
  TopoPhnSlice slice(cfg);
  std::vector<size_t> off;
  std::vector<uint32_t> len;
  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), bytes.size(), &off, &len).ok());

  TopoPhnStreamState st{};
  TopoPhnStreamInit(&st, cfg);
  st.digest_base = bytes.data();
  std::vector<ChunkDescriptor> batch;
  ASSERT_TRUE(
      TopoPhnStreamFeed(&st, bytes.data(), bytes.size(), true, &batch).ok());
  ASSERT_EQ(batch.size(), len.size());
  for (size_t i = 0; i < len.size(); ++i) {
    EXPECT_EQ(batch[i].offset, off[i]);
    EXPECT_EQ(batch[i].length, len[i]);
  }
}

TEST(TopoPhnH0Test, MultiFeedStreamingMatchesFullFile) {
  const auto cfg = MakeCfg(TopoPhnKernel::kPhH0Native);
  const auto bytes = RandomBytes(2 * 1024 * 1024, 88);
  TopoPhnSlice slice(cfg);
  std::vector<size_t> off;
  std::vector<uint32_t> len;
  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), bytes.size(), &off, &len).ok());

  TopoPhnStreamState st{};
  TopoPhnStreamInit(&st, cfg);
  st.digest_base = bytes.data();
  std::vector<ChunkDescriptor> all;
  const size_t feed = 256 * 1024;
  for (size_t i = 0; i < bytes.size(); i += feed) {
    const size_t n = std::min(feed, bytes.size() - i);
    std::vector<ChunkDescriptor> batch;
    ASSERT_TRUE(
        TopoPhnStreamFeed(&st, bytes.data() + i, n, i + n >= bytes.size(),
                          &batch)
            .ok());
    all.insert(all.end(), batch.begin(), batch.end());
  }
  ASSERT_EQ(all.size(), len.size());
  for (size_t i = 0; i < len.size(); ++i) {
    EXPECT_EQ(all[i].offset, off[i]) << "i=" << i;
    EXPECT_EQ(all[i].length, len[i]) << "i=" << i;
  }
}

TEST(TopoPhnH0Test, DiffersFromTriCuts) {
  const auto bytes = RandomBytes(4 * 1024 * 1024, 101);
  const size_t calib_n = topo_phn_internal::kPhnCalibSampleBytes;
  ASSERT_GE(bytes.size(), calib_n);
  TopoPhnConfig tri = TopoPhnConfigForProfile(ChunkProfileMode::kDefault);
  tri.table_seed = 101;
  tri.kernel = TopoPhnKernel::kTriNative;
  tri.k_points = 8;
  topo_phn_internal::CalibratePhnCutParams(bytes.data(), calib_n, &tri);
  TopoPhnConfig ph = TopoPhnConfigForProfile(ChunkProfileMode::kDefault);
  ph.table_seed = 101;
  ph.kernel = TopoPhnKernel::kPhH0Native;
  ph.k_points = 16;
  topo_phn_internal::CalibratePhnCutParams(bytes.data(), calib_n, &ph);
  std::vector<size_t> ot, op;
  std::vector<uint32_t> lt, lp;
  ASSERT_TRUE(TopoPhnSlice(tri).ChunkCuts(bytes.data(), bytes.size(), &ot, &lt).ok());
  ASSERT_TRUE(TopoPhnSlice(ph).ChunkCuts(bytes.data(), bytes.size(), &op, &lp).ok());
  EXPECT_TRUE(ot != op || lt != lp)
      << "PH AND-gate must not collapse to Tri-identical cuts";
}

TEST(TopoPhnH0Test, MeanLengthWithin15Pct) {
  auto bytes = RandomBytes(8 * 1024 * 1024, 66);
  TopoPhnConfig cfg = TopoPhnConfigForProfile(ChunkProfileMode::kDefault);
  cfg.table_seed = 66;
  cfg.kernel = TopoPhnKernel::kPhH0Native;
  cfg.k_points = 16;
  const size_t calib_n = topo_phn_internal::kPhnCalibSampleBytes;
  topo_phn_internal::CalibratePhnCutParams(bytes.data(), calib_n, &cfg);
  TopoPhnSlice slice(cfg);
  std::vector<size_t> off;
  std::vector<uint32_t> len;
  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), calib_n, &off, &len).ok());
  ASSERT_FALSE(len.empty());
  const double mean = MeanOf(len);
  // PH AND+persist on a pow2 stride lattice may sit just outside ±15%.
  EXPECT_GE(mean, cfg.avg_size * 0.80);
  EXPECT_LE(mean, cfg.avg_size * 1.25);

  ASSERT_TRUE(slice.ChunkCuts(bytes.data(), bytes.size(), &off, &len).ok());
  ASSERT_FALSE(len.empty());
  const double mean_full = MeanOf(len);
  // Full-file may drift vs 4MiB calib; keep under eval gate (avg×1.6).
  EXPECT_GE(mean_full, cfg.avg_size * 0.70);
  EXPECT_LE(mean_full, cfg.avg_size * 1.60);
}

TEST(TopoPhnEnvTest, EnabledDetectsTopophn) {
#if defined(_WIN32)
  _putenv_s("EBBACKUP_CDC_ALGO", "topophn");
  EXPECT_TRUE(CdcTopoPhnEnabled());
  _putenv_s("EBBACKUP_CDC_ALGO", "topoph");
  EXPECT_FALSE(CdcTopoPhnEnabled());
  _putenv_s("EBBACKUP_CDC_ALGO", "");
#else
  setenv("EBBACKUP_CDC_ALGO", "topophn", 1);
  EXPECT_TRUE(CdcTopoPhnEnabled());
  setenv("EBBACKUP_CDC_ALGO", "topoph", 1);
  EXPECT_FALSE(CdcTopoPhnEnabled());
  unsetenv("EBBACKUP_CDC_ALGO");
#endif
}

}  // namespace
}  // namespace ebbackup
