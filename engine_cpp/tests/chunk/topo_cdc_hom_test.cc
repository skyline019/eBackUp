#include <gtest/gtest.h>

#include <vector>

#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/topo_cdc.h"
#include "ebbackup/chunk/topo_cdc_internal.h"
#include "ebbackup/chunk/topo_cdc_streaming.h"
#include "test_util.h"

namespace ebbackup {
namespace {

uint16_t CalibrateForTest(uint32_t seed) {
  TopoCdcConfig cfg = TopoCdcConfigForProfile(ChunkProfileMode::kDefault);
  cfg.table_seed = seed;
  std::vector<uint8_t> sample(1024 * 1024);
  topo_cdc_internal::FillTopoCalibSample(sample.data(), sample.size(), seed);
  return topo_cdc_internal::CalibrateTopoPermille(sample.data(), sample.size(), cfg,
                                                    seed);
}

TopoCdcConfig MakeHomTestConfig(size_t file_size, uint32_t seed = 0x12345678u) {
  TopoCdcConfig cfg = TopoCdcConfigForFileSize(
      file_size, ChunkProfileMode::kAuto, DigestAlgo::kLegacy);
  cfg.table_seed = seed;
  cfg.topo_calib_permille = CalibrateForTest(seed);
  cfg.topo_shift = 1;
  cfg.variant = TopoCdcVariant::kHom;
  return cfg;
}

double MeanChunkLength(const std::vector<uint32_t>& lengths) {
  if (lengths.empty()) return 0.0;
  double mean = 0.0;
  for (uint32_t len : lengths) mean += static_cast<double>(len);
  return mean / static_cast<double>(lengths.size());
}

TEST(TopoCdcHomTest, IncrementalEdgeDiffMatchesSync) {
  uint32_t gear[256]{};
  topo_cdc_internal::InitGearTable(gear, 0x12345678u);
  constexpr size_t kLen = 64 * 1024;
  const std::string data = test::MakeRandomData(kLen, 17);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());

  for (uint32_t w : {4u, 8u, 16u, 32u, 64u, 128u}) {
    topo_cdc_internal::SlotUfWindow uf;
    uf.Reset(w);
    std::vector<uint8_t> init_keys(w);
    for (uint32_t i = 0; i < w; ++i) {
      init_keys[i] = static_cast<uint8_t>(gear[bytes[i]] & 0xFFu);
    }
    uf.LoadWindow(init_keys.data(), w);

    for (size_t p = w; p < kLen; ++p) {
      const uint8_t k = static_cast<uint8_t>(gear[bytes[p]] & 0xFFu);
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

TEST(TopoCdcHomTest, IncrementalUfMatchesRebuild) {
  uint32_t gear[256]{};
  topo_cdc_internal::InitGearTable(gear, 0x12345678u);
  constexpr size_t kLen = 64 * 1024;
  const std::string data = test::MakeRandomData(kLen, 17);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());

  for (uint32_t w : {4u, 8u, 16u, 32u, 64u, 128u}) {
    topo_cdc_internal::SlotUfWindow inc;
    topo_cdc_internal::SlotUfWindow ref;
    inc.Reset(w);
    ref.Reset(w);

    std::vector<uint8_t> init_keys(w);
    for (uint32_t i = 0; i < w; ++i) {
      init_keys[i] = static_cast<uint8_t>(gear[bytes[i]] & 0xFFu);
    }
    inc.LoadWindow(init_keys.data(), w);
    ref.LoadWindow(init_keys.data(), w);
    EXPECT_EQ(inc.ComponentCount(), ref.ComponentCount()) << "w=" << w;
    for (size_t p = w; p < kLen; ++p) {
      const uint8_t k = static_cast<uint8_t>(gear[bytes[p]] & 0xFFu);
      const int32_t d_inc = inc.Slide(k);
      const int32_t d_ref = ref.SlideViaRebuild(k);
      if (d_inc != d_ref || inc.ComponentCount() != ref.ComponentCount()) {
        FAIL() << "w=" << w << " p=" << p << " d_inc=" << d_inc << " d_ref=" << d_ref
               << " inc=" << inc.ComponentCount() << " ref=" << ref.ComponentCount();
      }
    }
  }
}

TEST(TopoCdcHomTest, CutPointsUnchangedAfterIncrementalUf) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 93);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  TopoCdcConfig cfg = MakeHomTestConfig(kFileSize);
  TopoCdcSlice chunker(cfg);
  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &offsets, &lengths).ok());
  ASSERT_GT(offsets.size(), 10u);
  EXPECT_GE(MeanChunkLength(lengths), static_cast<double>(cfg.avg_size) * 0.5);
}

TEST(TopoCdcHomTest, UfEventDeterministic) {
  topo_cdc_internal::SlotUfWindow uf;
  uf.Reset(4);
  const uint8_t keys[] = {1, 1, 2, 2};
  uf.LoadWindow(keys, 4);
  EXPECT_EQ(uf.ComponentCount(), 2u);
  const int32_t d1 = uf.Slide(3);
  EXPECT_NE(d1, 0);
  const int32_t d2 = uf.Slide(1);
  EXPECT_NE(d2, 0);
}

TEST(TopoCdcHomTest, MeanLengthWithin15Pct) {
  constexpr size_t kCalibSize = 4 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kCalibSize, 41);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  TopoCdcConfig cfg = MakeHomTestConfig(kCalibSize);
  cfg.topo_calib_permille = topo_cdc_internal::CalibrateTopoPermille(
      bytes, kCalibSize, cfg, cfg.table_seed);

  TopoCdcSlice chunker(cfg);
  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kCalibSize, &offsets, &lengths).ok());
  ASSERT_GT(lengths.size(), 3u);
  const double mean = MeanChunkLength(lengths);
  EXPECT_GE(mean, static_cast<double>(cfg.avg_size) * 0.85);
  EXPECT_LE(mean, static_cast<double>(cfg.avg_size) * 1.15);
}

TEST(TopoCdcHomTest, DeterministicGolden) {
  constexpr size_t kFileSize = 2 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 99);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  TopoCdcConfig cfg = MakeHomTestConfig(kFileSize);
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

TEST(TopoCdcHomTest, StreamingMatchesFullFile) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 93);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  TopoCdcConfig cfg = MakeHomTestConfig(kFileSize);
  TopoCdcSlice chunker(cfg);
  std::vector<size_t> full_off;
  std::vector<uint32_t> full_len;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &full_off, &full_len).ok());

  TopoCdcStreamState state{};
  TopoCdcStreamInit(&state, cfg);
  state.digest_base = bytes;

  std::vector<size_t> stream_off;
  std::vector<uint32_t> stream_len;
  std::vector<ChunkDescriptor> batch;
  ASSERT_TRUE(TopoCdcStreamFeed(&state, bytes, kFileSize, true, &batch).ok());
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

TEST(TopoCdcHomTest, StreamingMatchesMultiFeed512K) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  constexpr size_t kFeed = 512 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 93);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  TopoCdcConfig cfg = MakeHomTestConfig(kFileSize);
  TopoCdcSlice chunker(cfg);
  std::vector<size_t> full_off;
  std::vector<uint32_t> full_len;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &full_off, &full_len).ok());

  TopoCdcStreamState state{};
  TopoCdcStreamInit(&state, cfg);
  state.digest_base = bytes;

  std::vector<size_t> stream_off;
  std::vector<uint32_t> stream_len;
  for (size_t feed_off = 0; feed_off < kFileSize; feed_off += kFeed) {
    const size_t n = std::min(kFeed, kFileSize - feed_off);
    const bool last = (feed_off + n >= kFileSize);
    std::vector<ChunkDescriptor> batch;
    ASSERT_TRUE(
        TopoCdcStreamFeed(&state, bytes + feed_off, n, last, &batch).ok());
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

}  // namespace
}  // namespace ebbackup
