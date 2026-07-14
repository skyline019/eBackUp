#include <gtest/gtest.h>

#include <vector>

#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/topo_cdc.h"
#include "ebbackup/chunk/topo_cdc_internal.h"
#include "ebbackup/chunk/topo_cdc_streaming.h"
#include "ebbackup/chunk/topo_tri_internal.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TopoCdcConfig MakeTriTestConfig(size_t file_size, uint32_t seed = 0xABCDEF01u) {
  TopoCdcConfig cfg = TopoCdcConfigForFileSize(
      file_size, ChunkProfileMode::kAuto, DigestAlgo::kLegacy);
  cfg.variant = TopoCdcVariant::kTri;
  cfg.table_seed = seed;
  cfg.topo_calib_permille = 500;
  cfg.tri_k_points = 16;
  cfg.tri_q_mod = 65521;
  return cfg;
}

TEST(TopoCdcTriTest, DelaunayFlipCountBasic) {
  uint32_t h[] = {100, 200, 300, 400};
  uint32_t t[] = {0, 1, 2, 3};
  EXPECT_GE(topo_tri_internal::DelaunayFlipCount(h, t, 4, 16, 65521), 1u);
}

TEST(TopoCdcTriTest, DeterministicGolden) {
  constexpr size_t kFileSize = 2 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 88);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  TopoCdcConfig cfg = MakeTriTestConfig(kFileSize);
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

TEST(TopoCdcTriTest, StreamingMatchesFullFile) {
  constexpr size_t kFileSize = 4 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 77);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  TopoCdcConfig cfg = MakeTriTestConfig(kFileSize);
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

}  // namespace
}  // namespace ebbackup
