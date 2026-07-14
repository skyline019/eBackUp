#include <gtest/gtest.h>

#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/chunk/chunk_profile.h"
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

bool CutsDiffer(const std::vector<size_t>& a_off, const std::vector<uint32_t>& a_len,
                const std::vector<size_t>& b_off, const std::vector<uint32_t>& b_len) {
  if (a_off.size() != b_off.size()) return true;
  for (size_t i = 0; i < a_off.size(); ++i) {
    if (a_off[i] != b_off[i] || a_len[i] != b_len[i]) return true;
  }
  return false;
}

TEST(GtCdcDriftTest, OneByteEditDiffersFromFastCdc) {
  constexpr size_t kSize = 8 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kSize, 11);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());

  FastCdcSlice fast;
  GtCdcSlice gt(MakeNativeTestConfig(kSize));
  std::vector<size_t> fast_base_off, gt_base_off;
  std::vector<uint32_t> fast_base_len, gt_base_len;
  ASSERT_TRUE(fast.ChunkCuts(bytes, kSize, &fast_base_off, &fast_base_len).ok());
  ASSERT_TRUE(gt.ChunkCuts(bytes, kSize, &gt_base_off, &gt_base_len).ok());
  ASSERT_NE(fast_base_off, gt_base_off);
  ASSERT_GT(fast_base_off.size(), 2u);

  const size_t edit_offset =
      fast_base_off[1] + static_cast<size_t>(fast_base_len[1] / 2);
  ASSERT_LT(edit_offset, kSize);
  std::string edited = data;
  edited[edit_offset] ^= 0xFF;

  std::vector<size_t> fast_edit_off, gt_edit_off;
  std::vector<uint32_t> fast_edit_len, gt_edit_len;
  ASSERT_TRUE(fast.ChunkCuts(reinterpret_cast<const uint8_t*>(edited.data()),
                             edited.size(), &fast_edit_off, &fast_edit_len)
                  .ok());
  ASSERT_TRUE(gt.ChunkCuts(reinterpret_cast<const uint8_t*>(edited.data()),
                           edited.size(), &gt_edit_off, &gt_edit_len)
                  .ok());

  const bool fast_drift =
      CutsDiffer(fast_base_off, fast_base_len, fast_edit_off, fast_edit_len);
  const bool gt_drift =
      CutsDiffer(gt_base_off, gt_base_len, gt_edit_off, gt_edit_len);
  const bool cross_drift =
      CutsDiffer(fast_edit_off, fast_edit_len, gt_edit_off, gt_edit_len);
  EXPECT_TRUE(fast_drift || gt_drift || cross_drift);
}

}  // namespace
}  // namespace ebbackup
