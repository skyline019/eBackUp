#include <gtest/gtest.h>

#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/chunk/gt_cdc.h"
#include "ebbackup/chunk/gt_cdc_internal.h"
#include "test_util.h"

namespace ebbackup {
namespace {

GtCdcConfig MakeNativeTestConfig(size_t file_size) {
  GtCdcConfig cfg = GtCdcConfigForFileSize(
      file_size, ChunkProfileMode::kAuto, DigestAlgo::kLegacy,
      GtCdcKernel::kNative);
  cfg.table_seed = 0x12345678u;
  cfg.nc_level = 2;
  gtcdc_internal::InitGearTableForConfig(&cfg);
  return cfg;
}

bool CutsEqual(const std::vector<size_t>& a_off, const std::vector<uint32_t>& a_len,
               const std::vector<size_t>& b_off, const std::vector<uint32_t>& b_len) {
  if (a_off.size() != b_off.size()) return false;
  for (size_t i = 0; i < a_off.size(); ++i) {
    if (a_off[i] != b_off[i] || a_len[i] != b_len[i]) return false;
  }
  return true;
}

TEST(GtCdcTest, EmptyInput) {
  GtCdcSlice chunker;
  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  ASSERT_TRUE(chunker.ChunkCutsScalar(nullptr, 0, &offsets, &lengths).ok());
  EXPECT_TRUE(offsets.empty());
  EXPECT_TRUE(lengths.empty());
}

TEST(GtCdcTest, SmallFileSingleChunk) {
  const std::string data = test::MakeSyntheticData(1024, 7);
  GtCdcSlice chunker;
  std::vector<ChunkDescriptor> chunks;
  ASSERT_TRUE(chunker.Chunk(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), &chunks)
                  .ok());
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].offset, 0u);
  EXPECT_EQ(chunks[0].length, data.size());
}

TEST(GtCdcTest, DeterministicBoundaries) {
  const std::string data = test::MakeSyntheticData(3 * 1024 * 1024, 42);
  GtCdcSlice chunker;
  std::vector<size_t> a_off;
  std::vector<uint32_t> a_len;
  std::vector<size_t> b_off;
  std::vector<uint32_t> b_len;
  ASSERT_TRUE(
      chunker.ChunkCutsScalar(reinterpret_cast<const uint8_t*>(data.data()),
                              data.size(), &a_off, &a_len)
          .ok());
  ASSERT_TRUE(
      chunker.ChunkCutsScalar(reinterpret_cast<const uint8_t*>(data.data()),
                              data.size(), &b_off, &b_len)
          .ok());
  ASSERT_EQ(a_off.size(), b_off.size());
  for (size_t i = 0; i < a_off.size(); ++i) {
    EXPECT_EQ(a_off[i], b_off[i]) << "offset index " << i;
    EXPECT_EQ(a_len[i], b_len[i]) << "length index " << i;
  }
}

TEST(GtCdcTest, DiffersFromFastCdc) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 11);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcSlice gt(MakeNativeTestConfig(data.size()));
  FastCdcSlice fast;
  std::vector<size_t> gt_off;
  std::vector<uint32_t> gt_len;
  std::vector<size_t> fast_off;
  std::vector<uint32_t> fast_len;
  ASSERT_TRUE(gt.ChunkCuts(bytes, data.size(), &gt_off, &gt_len).ok());
  ASSERT_TRUE(fast.ChunkCuts(bytes, data.size(), &fast_off, &fast_len).ok());
  EXPECT_FALSE(CutsEqual(gt_off, gt_len, fast_off, fast_len))
      << "G-TCDC v4 native and FastCDC should not be bit-identical";
}

TEST(GtCdcTest, DiffersFromV3Gear) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 13);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig gear_cfg = GtCdcConfigForFileSize(
      data.size(), ChunkProfileMode::kAuto, DigestAlgo::kLegacy,
      GtCdcKernel::kGear);
  GtCdcSlice gear(gear_cfg);
  GtCdcSlice native(MakeNativeTestConfig(data.size()));
  std::vector<size_t> gear_off;
  std::vector<uint32_t> gear_len;
  std::vector<size_t> native_off;
  std::vector<uint32_t> native_len;
  ASSERT_TRUE(gear.ChunkCuts(bytes, data.size(), &gear_off, &gear_len).ok());
  ASSERT_TRUE(native.ChunkCuts(bytes, data.size(), &native_off, &native_len).ok());
  EXPECT_FALSE(CutsEqual(gear_off, gear_len, native_off, native_len))
      << "G-TCDC v4 native and v3 gear should not be bit-identical";
}

TEST(GtCdcTest, NativeGoldenStable) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string data = test::MakeSyntheticData(kFileSize, 99);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcConfig cfg = GtCdcConfigForFileSize(
      kFileSize, ChunkProfileMode::kDefault, DigestAlgo::kLegacy,
      GtCdcKernel::kNative);
  cfg.table_seed = 0x12345678u;
  cfg.nc_level = 2;
  gtcdc_internal::InitGearTableForConfig(&cfg);
  GtCdcSlice chunker(cfg);
  std::vector<size_t> off_a;
  std::vector<uint32_t> len_a;
  std::vector<size_t> off_b;
  std::vector<uint32_t> len_b;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &off_a, &len_a).ok());
  ASSERT_TRUE(chunker.ChunkCuts(bytes, kFileSize, &off_b, &len_b).ok());
  ASSERT_EQ(off_a, off_b);
  ASSERT_EQ(len_a, len_b);
  ASSERT_GT(off_a.size(), 100u);
}

TEST(GtCdcTest, LargeSyntheticGoldenStable) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string data = test::MakeSyntheticData(kFileSize, 99);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcSlice chunker;
  std::vector<size_t> off_a;
  std::vector<uint32_t> len_a;
  std::vector<size_t> off_b;
  std::vector<uint32_t> len_b;
  ASSERT_TRUE(chunker.ChunkCutsScalar(bytes, kFileSize, &off_a, &len_a).ok());
  ASSERT_TRUE(chunker.ChunkCutsScalar(bytes, kFileSize, &off_b, &len_b).ok());
  ASSERT_EQ(off_a, off_b);
  ASSERT_EQ(len_a, len_b);
  ASSERT_GT(off_a.size(), 100u);
}

TEST(GtCdcTest, TensorPathDeterministic) {
  const std::string data = test::MakeSyntheticData(2 * 1024 * 1024, 17);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  GtCdcSlice chunker;
  std::vector<size_t> off;
  std::vector<uint32_t> len;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, data.size(), &off, &len).ok());
  ASSERT_GT(off.size(), 1u);
}

}  // namespace
}  // namespace ebbackup
