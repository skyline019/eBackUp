#include <gtest/gtest.h>

#include <cmath>
#include <vector>

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

double ChunkLengthCv(const std::vector<uint32_t>& lengths) {
  if (lengths.size() < 2) return 0.0;
  double mean = 0.0;
  for (uint32_t len : lengths) mean += static_cast<double>(len);
  mean /= static_cast<double>(lengths.size());
  if (mean <= 0.0) return 0.0;
  double var = 0.0;
  for (uint32_t len : lengths) {
    const double d = static_cast<double>(len) - mean;
    var += d * d;
  }
  var /= static_cast<double>(lengths.size() - 1);
  return std::sqrt(var) / mean;
}

TEST(GtCdcNcTest, NativeDiffersFromGearAndTighterDistribution) {
  constexpr size_t kFileSize = 8 * 1024 * 1024;
  const std::string data = test::MakeRandomData(kFileSize, 37);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());

  GtCdcConfig gear_cfg = GtCdcConfigForFileSize(
      kFileSize, ChunkProfileMode::kAuto, DigestAlgo::kLegacy,
      GtCdcKernel::kGear);
  GtCdcSlice gear(gear_cfg);
  GtCdcSlice native(MakeNativeTestConfig(kFileSize));

  std::vector<size_t> gear_off;
  std::vector<uint32_t> gear_len;
  std::vector<size_t> native_off;
  std::vector<uint32_t> native_len;
  ASSERT_TRUE(gear.ChunkCuts(bytes, kFileSize, &gear_off, &gear_len).ok());
  ASSERT_TRUE(native.ChunkCuts(bytes, kFileSize, &native_off, &native_len).ok());
  EXPECT_FALSE(gear_off == native_off && gear_len == native_len);
  ASSERT_GT(gear_len.size(), 3u);
  ASSERT_GT(native_len.size(), 3u);

  const double gear_cv = ChunkLengthCv(gear_len);
  const double native_cv = ChunkLengthCv(native_len);
  EXPECT_LT(native_cv, gear_cv)
      << "NC native CV=" << native_cv << " gear CV=" << gear_cv;
}

}  // namespace
}  // namespace ebbackup
