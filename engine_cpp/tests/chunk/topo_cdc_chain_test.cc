#include <gtest/gtest.h>

#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/topo_cdc.h"
#include "ebbackup/chunk/topo_cdc_internal.h"
#include "ebbackup/chunk/topo_chain_internal.h"
#include "ebbackup/chunk/topo_chain_streaming.h"
#include "ebbackup/engine/manifest.h"
#include "test_util.h"

namespace ebbackup {
namespace {

uint8_t CalibrateChainForTest(uint32_t seed, bool enable_beta1) {
  TopoCdcConfig cfg = TopoCdcConfigForProfile(ChunkProfileMode::kDefault);
  cfg.variant = TopoCdcVariant::kChain;
  cfg.chain_lfsr_seed = seed;
  cfg.chain_enable_beta1 = enable_beta1;
  std::vector<uint8_t> sample(1024 * 1024);
  topo_chain_internal::FillChainCalibSample(sample.data(), sample.size(), seed);
  return static_cast<uint8_t>(
      topo_chain_internal::CalibrateChainStrideLog(sample.data(), sample.size(),
                                                   cfg) &
      0xFFu);
}

TopoCdcConfig MakeChainTestConfig(size_t file_size, uint32_t seed = 0x12345678u,
                                  bool enable_beta1 = true) {
  TopoCdcConfig cfg = TopoCdcConfigForFileSize(
      file_size, ChunkProfileMode::kAuto, DigestAlgo::kLegacy);
  cfg.variant = TopoCdcVariant::kChain;
  cfg.chain_lfsr_seed = seed;
  cfg.chain_stride_log = CalibrateChainForTest(seed, enable_beta1);
  cfg.chain_quant_q = 0;
  cfg.chain_enable_beta1 = enable_beta1;
  return cfg;
}

double MeanChunkLength(const std::vector<uint32_t>& lengths) {
  if (lengths.empty()) return 0.0;
  double mean = 0.0;
  for (uint32_t len : lengths) mean += static_cast<double>(len);
  return mean / static_cast<double>(lengths.size());
}

TEST(TopoCdcChainTest, ChainEdgeDiffMatchesSync) {
  constexpr size_t kLen = 64 * 1024;
  const std::string data = test::MakeRandomData(kLen, 17);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  const uint8_t quant_q = 0;

  for (uint32_t w : {4u, 8u, 16u, 32u, 64u, 128u}) {
    topo_cdc_internal::SlotUfWindow uf;
    uf.Reset(w);
    std::vector<uint8_t> init_keys(w);
    for (uint32_t i = 0; i < w; ++i) {
      init_keys[i] = topo_chain_internal::ChainQuantKey(bytes[i], quant_q);
    }
    uf.LoadWindow(init_keys.data(), w);

    for (size_t p = w; p < kLen; ++p) {
      const uint8_t k = topo_chain_internal::ChainQuantKey(bytes[p], quant_q);
      const uint32_t head_old = uf.head;
      const uint32_t tail = (head_old + w - 1) % w;
      const uint8_t old_tail = uf.key[tail];
      const uint32_t ed_pred =
          topo_cdc_internal::ApplySlideEdgeDiffO1(uf, k, head_old, old_tail);
      uf.edge_diff = ed_pred;
      uf.head = (head_old + 1) % w;
      uf.key[tail] = k;
      uf.RecountEdgeDiff();
      EXPECT_EQ(ed_pred, uf.edge_diff) << "w=" << w << " p=" << p;
    }
  }
}

TEST(TopoCdcChainTest, DeterministicGolden) {
  constexpr size_t kFileSize = 2 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 99);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  TopoCdcConfig cfg = MakeChainTestConfig(kFileSize);
  TopoCdcSlice a(cfg);
  TopoCdcSlice b(cfg);
  std::vector<size_t> off_a;
  std::vector<uint32_t> len_a;
  std::vector<size_t> off_b;
  std::vector<uint32_t> len_b;
  ASSERT_TRUE(a.ChunkCuts(bytes, kFileSize, &off_a, &len_a).ok());
  ASSERT_TRUE(b.ChunkCuts(bytes, kFileSize, &off_b, &len_b).ok());
  EXPECT_EQ(off_a, off_b);
  EXPECT_EQ(len_a, len_b);
}

TEST(TopoCdcChainTest, StreamingMatchesFullFile) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 93);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  TopoCdcConfig cfg = MakeChainTestConfig(kFileSize);
  TopoCdcSlice chunker(cfg);
  std::vector<size_t> full_off;
  std::vector<uint32_t> full_len;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &full_off, &full_len).ok());

  TopoChainStreamState state{};
  TopoChainStreamInit(&state, cfg);
  state.digest_base = bytes;

  std::vector<size_t> stream_off;
  std::vector<uint32_t> stream_len;
  std::vector<ChunkDescriptor> batch;
  ASSERT_TRUE(TopoChainStreamFeed(&state, bytes, kFileSize, true, &batch).ok());
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

TEST(TopoCdcChainTest, StreamingMatchesMultiFeed512K) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  constexpr size_t kFeed = 512 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 93);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  TopoCdcConfig cfg = MakeChainTestConfig(kFileSize);
  TopoCdcSlice chunker(cfg);
  std::vector<size_t> full_off;
  std::vector<uint32_t> full_len;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &full_off, &full_len).ok());

  TopoChainStreamState state{};
  TopoChainStreamInit(&state, cfg);
  state.digest_base = bytes;

  std::vector<size_t> stream_off;
  std::vector<uint32_t> stream_len;
  for (size_t feed_off = 0; feed_off < kFileSize; feed_off += kFeed) {
    const size_t n = std::min(kFeed, kFileSize - feed_off);
    const bool last = (feed_off + n >= kFileSize);
    std::vector<ChunkDescriptor> batch;
    ASSERT_TRUE(
        TopoChainStreamFeed(&state, bytes + feed_off, n, last, &batch).ok());
    for (const auto& d : batch) {
      stream_off.push_back(d.offset);
      stream_len.push_back(d.length);
    }
  }

  ASSERT_EQ(full_off.size(), stream_off.size());
  for (size_t i = 0; i < full_off.size(); ++i) {
    EXPECT_EQ(full_off[i], stream_off[i]) << " at index " << i;
    EXPECT_EQ(full_len[i], stream_len[i]) << " at index " << i;
  }
}

TEST(TopoCdcChainTest, MeanLengthWithin15Pct) {
  constexpr size_t kCalibSize = 1024 * 1024;
  const uint32_t seed = 0x12345678u;
  TopoCdcConfig cfg = TopoCdcConfigForProfile(ChunkProfileMode::kDefault);
  cfg.variant = TopoCdcVariant::kChain;
  cfg.chain_lfsr_seed = seed;
  std::vector<uint8_t> sample(kCalibSize);
  topo_chain_internal::FillChainCalibSample(sample.data(), sample.size(), seed);
  cfg.chain_stride_log = static_cast<uint8_t>(
      topo_chain_internal::CalibrateChainStrideLog(sample.data(), sample.size(),
                                                   cfg) &
      0xFFu);

  TopoCdcSlice chunker(cfg);
  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  ASSERT_TRUE(chunker.ChunkCuts(sample.data(), sample.size(), &offsets, &lengths)
                  .ok());
  ASSERT_GT(lengths.size(), 3u);
  const double mean = MeanChunkLength(lengths);
  EXPECT_GE(mean, static_cast<double>(cfg.avg_size) * 0.85);
  EXPECT_LE(mean, static_cast<double>(cfg.avg_size) * 1.15);
}

TEST(TopoCdcChainTest, NoGearTableDependency) {
  TopoCdcConfig cfg = MakeChainTestConfig(1024 * 1024);
  EXPECT_EQ(cfg.variant, TopoCdcVariant::kChain);
  TopoCdcSlice slice(cfg);
  const std::string data = test::MakeRandomData(512 * 1024, 7);
  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  ASSERT_TRUE(slice.ChunkCuts(reinterpret_cast<const uint8_t*>(data.data()),
                              data.size(), &offsets, &lengths)
                  .ok());
  EXPECT_GT(lengths.size(), 0u);
}

TEST(TopoCdcChainTest, Beta1Gf2MatchesManual) {
  const uint8_t keys[] = {1, 2, 1, 3, 2, 1, 4, 3};
  const uint32_t w = 8;
  const uint32_t beta1 = topo_chain_internal::ChainBeta1Gf2(keys, w);
  EXPECT_GT(beta1, 0u);

  const uint8_t line[] = {5, 5, 5, 5, 5, 5, 5, 5};
  EXPECT_EQ(topo_chain_internal::ChainBeta1Gf2(line, w), 0u);
}

TEST(TopoCdcChainTest, Beta1EnabledChangesCuts) {
  constexpr size_t kFileSize = 512 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 42);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  bool found_diff = false;
  for (uint32_t seed = 1; seed <= 32 && !found_diff; ++seed) {
    TopoCdcConfig cfg_off = MakeChainTestConfig(kFileSize, seed, false);
    TopoCdcConfig cfg_on = MakeChainTestConfig(kFileSize, seed, true);
    cfg_off.window_w = 8;
    cfg_on.window_w = 8;
    cfg_off.chain_stride_log = 0;
    cfg_on.chain_stride_log = 0;
    TopoCdcSlice off(cfg_off);
    TopoCdcSlice on(cfg_on);
    std::vector<size_t> off_o;
    std::vector<uint32_t> len_o;
    std::vector<size_t> off_n;
    std::vector<uint32_t> len_n;
    ASSERT_TRUE(off.ChunkCuts(bytes, kFileSize, &off_o, &len_o).ok());
    ASSERT_TRUE(on.ChunkCuts(bytes, kFileSize, &off_n, &len_n).ok());
    if (off_o != off_n) found_diff = true;
  }
  EXPECT_TRUE(found_diff);
}

TEST(TopoCdcChainTest, ParallelMatchesSerial) {
  constexpr size_t kFileSize = 4 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 55);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  TopoCdcConfig cfg = MakeChainTestConfig(kFileSize);

#if defined(_WIN32)
  _putenv_s("EBBACKUP_TOPOCHAIN_PARALLEL_SCAN", "0");
#else
  setenv("EBBACKUP_TOPOCHAIN_PARALLEL_SCAN", "0", 1);
#endif
  TopoCdcSlice serial(cfg);
  std::vector<size_t> off_s;
  std::vector<uint32_t> len_s;
  ASSERT_TRUE(serial.ChunkCuts(bytes, kFileSize, &off_s, &len_s).ok());

#if defined(_WIN32)
  _putenv_s("EBBACKUP_TOPOCHAIN_PARALLEL_SCAN", "1");
#else
  setenv("EBBACKUP_TOPOCHAIN_PARALLEL_SCAN", "1", 1);
#endif
  TopoCdcSlice parallel(cfg);
  std::vector<size_t> off_p;
  std::vector<uint32_t> len_p;
  ASSERT_TRUE(parallel.ChunkCuts(bytes, kFileSize, &off_p, &len_p).ok());

#if defined(_WIN32)
  _putenv_s("EBBACKUP_TOPOCHAIN_PARALLEL_SCAN", "0");
#else
  setenv("EBBACKUP_TOPOCHAIN_PARALLEL_SCAN", "0", 1);
#endif

  EXPECT_EQ(off_s, off_p);
  EXPECT_EQ(len_s, len_p);
}

void ExpectFullStreamParallelParity(const uint8_t* bytes, size_t len,
                                    const TopoCdcConfig& cfg) {
  TopoCdcSlice full(cfg);
  std::vector<size_t> off_f;
  std::vector<uint32_t> len_f;
  ASSERT_TRUE(full.ChunkCuts(bytes, len, &off_f, &len_f).ok());

  TopoChainStreamState state{};
  TopoChainStreamInit(&state, cfg);
  state.digest_base = bytes;
  std::vector<ChunkDescriptor> batch;
  ASSERT_TRUE(TopoChainStreamFeed(&state, bytes, len, true, &batch).ok());
  ASSERT_EQ(off_f.size(), batch.size());
  for (size_t i = 0; i < off_f.size(); ++i) {
    EXPECT_EQ(off_f[i], batch[i].offset);
    EXPECT_EQ(len_f[i], batch[i].length);
  }

#if defined(_WIN32)
  _putenv_s("EBBACKUP_TOPOCHAIN_PARALLEL_SCAN", "1");
#else
  setenv("EBBACKUP_TOPOCHAIN_PARALLEL_SCAN", "1", 1);
#endif
  TopoCdcSlice par(cfg);
  std::vector<size_t> off_p;
  std::vector<uint32_t> len_p;
  ASSERT_TRUE(par.ChunkCuts(bytes, len, &off_p, &len_p).ok());
#if defined(_WIN32)
  _putenv_s("EBBACKUP_TOPOCHAIN_PARALLEL_SCAN", "0");
#else
  setenv("EBBACKUP_TOPOCHAIN_PARALLEL_SCAN", "0", 1);
#endif
  EXPECT_EQ(off_f, off_p);
  EXPECT_EQ(len_f, len_p);
}

TEST(TopoCdcChainTest, Beta1ImplParity) {
  std::mt19937 rng(0xC0FFEEu);
  for (int trial = 0; trial < 200; ++trial) {
    const uint32_t w = 4 + (rng() % 60);
    std::vector<uint8_t> keys(w);
    for (uint32_t i = 0; i < w; ++i) keys[i] = static_cast<uint8_t>(rng() & 0xFFu);
    EXPECT_EQ(topo_chain_internal::ChainBeta1Gf2(keys.data(), w),
              topo_chain_internal::ChainBeta1Gf2Reference(keys.data(), w))
        << "trial=" << trial << " w=" << w;
  }
}

TEST(TopoCdcChainTest, AllZeroDataParity) {
  constexpr size_t kFileSize = 2 * 1024 * 1024;
  std::vector<uint8_t> data(kFileSize, 0);
  TopoCdcConfig cfg = MakeChainTestConfig(kFileSize, 0x1111u);
  ExpectFullStreamParallelParity(data.data(), kFileSize, cfg);
}

TEST(TopoCdcChainTest, ConstantByteParity) {
  constexpr size_t kFileSize = 2 * 1024 * 1024;
  std::vector<uint8_t> data(kFileSize, 0xA5);
  TopoCdcConfig cfg = MakeChainTestConfig(kFileSize, 0x2222u);
  ExpectFullStreamParallelParity(data.data(), kFileSize, cfg);
}

TEST(TopoCdcChainTest, AdversarialPeriodLFSR) {
  constexpr size_t kFileSize = 2 * 1024 * 1024;
  std::vector<uint8_t> data(kFileSize);
  for (size_t i = 0; i < kFileSize; ++i) {
    data[i] = static_cast<uint8_t>((i % 17) * 13);
  }
  TopoCdcConfig cfg = MakeChainTestConfig(kFileSize, 0x3333u);
  TopoCdcSlice slice(cfg);
  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  ASSERT_TRUE(slice.ChunkCuts(data.data(), kFileSize, &offsets, &lengths).ok());
  ASSERT_FALSE(lengths.empty());
  for (uint32_t L : lengths) {
    EXPECT_GE(L, cfg.min_size);
    EXPECT_LE(L, cfg.max_size);
  }
  ExpectFullStreamParallelParity(data.data(), kFileSize, cfg);
}

TEST(TopoCdcChainTest, WindowSlideIncrementalParity) {
  constexpr size_t kFileSize = 1 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 71);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  TopoCdcConfig cfg = MakeChainTestConfig(kFileSize);
  TopoCdcSlice a(cfg);
  TopoCdcSlice b(cfg);
  std::vector<size_t> off_a, off_b;
  std::vector<uint32_t> len_a, len_b;
  ASSERT_TRUE(a.ChunkCuts(bytes, kFileSize, &off_a, &len_a).ok());
  ASSERT_TRUE(b.ChunkCuts(bytes, kFileSize, &off_b, &len_b).ok());
  EXPECT_EQ(off_a, off_b);
  EXPECT_EQ(len_a, len_b);
}

TEST(TopoCdcChainTest, MvpBeta1FeatureBitPreserved) {
  BackupSuperBlock sb{};
  sb.ext.topo_variant = 2;
  sb.ext.backup_features |= kBackupFeatureTopoChain;
  SetRepoTopoChainStrideLog(&sb, 12);
  SetRepoTopoChainQuantQ(&sb, 0);
  SetRepoTopoChainFeatures(&sb, 0);  // MVP: no beta1
  EXPECT_FALSE(RepoTopoChainBeta1(sb));
  TopoCdcConfig cfg = TopoCdcConfigForRepo(sb, 1024 * 1024, ChunkProfileMode::kDefault,
                                           DigestAlgo::kLegacy);
  EXPECT_FALSE(cfg.chain_enable_beta1);

  SetRepoTopoChainBeta1(&sb, true);
  EXPECT_TRUE(RepoTopoChainBeta1(sb));
  cfg = TopoCdcConfigForRepo(sb, 1024 * 1024, ChunkProfileMode::kDefault,
                             DigestAlgo::kLegacy);
  EXPECT_TRUE(cfg.chain_enable_beta1);
}

}  // namespace
}  // namespace ebbackup
