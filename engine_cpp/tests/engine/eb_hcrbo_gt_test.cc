#include <gtest/gtest.h>

#include <cstring>

#include "ebbackup/chunk/eb_hcrbo_gt.h"
#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/gt_cdc_internal.h"
#include "ebbackup/chunk/rolling_checksum.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

EbHcrboGtConfig MakeNativeHcrboConfig(size_t file_size) {
  EbHcrboGtConfig cfg = EbHcrboGtConfigForFileSize(
      file_size, ChunkProfileMode::kDefault, DigestAlgo::kLegacy,
      GtCdcKernel::kNative);
  cfg.gt.table_seed = 0x12345678u;
  cfg.gt.nc_level = 2;
  gtcdc_internal::InitGearTableForConfig(&cfg.gt);
  return cfg;
}

EbHcrboGtConfig MakeAnGearHcrboConfig(size_t file_size) {
  EbHcrboGtConfig cfg = EbHcrboGtConfigForFileSize(
      file_size, ChunkProfileMode::kDefault, DigestAlgo::kLegacy,
      GtCdcKernel::kAnGear);
  cfg.gt.table_seed = 0x12345678u;
  cfg.gt.nc_level = 2;
  gtcdc_internal::InitGearTableForConfig(&cfg.gt);
  return cfg;
}

void PopulateRolling(const uint8_t* data, size_t len, CfiIndex* cfi) {
  for (auto& a : cfi->anchors) {
    if (a.offset + a.length <= len) {
      a.rolling_checksum = RollingChecksum(data + a.offset, a.length);
    }
  }
}

TEST(EbHcrboGtTest, UnchangedFileFullReuse) {
  const std::string data = test::MakeSyntheticData(2 * 1024 * 1024, 5);
  EbHcrboGtConfig cfg = MakeNativeHcrboConfig(data.size());
  EbHcrboGtChunker chunker(cfg);
  std::vector<ChunkDescriptor> full;
  CfiIndex cfi;
  EbHcrboStats stats{};
  ASSERT_TRUE(chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                                data.size(), &full, &cfi, &stats)
                  .ok());
  PopulateRolling(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                  &cfi);

  std::vector<ChunkDescriptor> incr;
  CfiIndex cfi_out;
  EbHcrboStats incr_stats{};
  ASSERT_TRUE(
      chunker
          .ChunkIncremental(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), cfi, &incr, &cfi_out, &incr_stats)
          .ok());
  EXPECT_EQ(incr.size(), full.size());
  EXPECT_EQ(incr_stats.chunks_reused_from_cfi, full.size());
}

TEST(EbHcrboGtTest, OneByteEditHighReuse) {
  std::string data = test::MakeSyntheticData(20 * 1024 * 1024, 8);
  EbHcrboGtConfig cfg = MakeNativeHcrboConfig(data.size());
  EbHcrboGtChunker chunker(cfg);
  std::vector<ChunkDescriptor> full;
  CfiIndex cfi;
  ASSERT_TRUE(chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                                data.size(), &full, &cfi, nullptr)
                  .ok());
  ASSERT_GE(full.size(), 20u);
  PopulateRolling(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                  &cfi);

  data[5 * 1024 * 1024] ^= 0x01;
  std::vector<ChunkDescriptor> incr;
  CfiIndex cfi_out;
  EbHcrboStats incr_stats{};
  ASSERT_TRUE(
      chunker
          .ChunkIncremental(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), cfi, &incr, &cfi_out, &incr_stats)
          .ok());

  const double reuse_pct =
      full.empty()
          ? 0.0
          : (100.0 * static_cast<double>(incr_stats.chunks_reused_from_cfi) /
             static_cast<double>(full.size()));
  EXPECT_GE(reuse_pct, 95.0);
}

TEST(EbHcrboGtTest, IncrementalMatchesFullNative) {
  std::string data = test::MakeSyntheticData(8 * 1024 * 1024, 11);
  data[1024 * 1024] ^= 0x5A;
  EbHcrboGtConfig cfg = MakeNativeHcrboConfig(data.size());
  EbHcrboGtChunker chunker(cfg);

  std::vector<ChunkDescriptor> baseline_full;
  CfiIndex baseline_cfi;
  ASSERT_TRUE(
      chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                        data.size(), &baseline_full, &baseline_cfi, nullptr)
          .ok());
  PopulateRolling(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                  &baseline_cfi);

  std::vector<ChunkDescriptor> incr;
  CfiIndex cfi_out;
  ASSERT_TRUE(
      chunker
          .ChunkIncremental(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), baseline_cfi, &incr, &cfi_out,
                            nullptr)
          .ok());

  std::vector<ChunkDescriptor> direct_full;
  ASSERT_TRUE(
      chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                        data.size(), &direct_full, nullptr, nullptr)
          .ok());

  ASSERT_EQ(incr.size(), direct_full.size());
  for (size_t i = 0; i < incr.size(); ++i) {
    EXPECT_EQ(incr[i].offset, direct_full[i].offset);
    EXPECT_EQ(incr[i].length, direct_full[i].length);
    EXPECT_EQ(std::memcmp(incr[i].hash, direct_full[i].hash, 32), 0);
  }
}

TEST(EbHcrboGtTest, IncrementalMatchesFullAnGear) {
  std::string data = test::MakeRandomData(8 * 1024 * 1024, 11);
  data[1024 * 1024] ^= 0x5A;
  EbHcrboGtConfig cfg = MakeAnGearHcrboConfig(data.size());
  EbHcrboGtChunker chunker(cfg);

  std::vector<ChunkDescriptor> baseline_full;
  CfiIndex baseline_cfi;
  ASSERT_TRUE(
      chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                        data.size(), &baseline_full, &baseline_cfi, nullptr)
          .ok());
  PopulateRolling(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                  &baseline_cfi);

  std::vector<ChunkDescriptor> incr;
  CfiIndex cfi_out;
  ASSERT_TRUE(
      chunker
          .ChunkIncremental(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), baseline_cfi, &incr, &cfi_out,
                            nullptr)
          .ok());

  std::vector<ChunkDescriptor> direct_full;
  ASSERT_TRUE(
      chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                        data.size(), &direct_full, nullptr, nullptr)
          .ok());

  ASSERT_EQ(incr.size(), direct_full.size());
  for (size_t i = 0; i < incr.size(); ++i) {
    EXPECT_EQ(incr[i].offset, direct_full[i].offset);
    EXPECT_EQ(incr[i].length, direct_full[i].length);
    EXPECT_EQ(std::memcmp(incr[i].hash, direct_full[i].hash, 32), 0);
  }
}

}  // namespace
}  // namespace ebbackup
