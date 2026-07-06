#include <gtest/gtest.h>

#include <cstring>

#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/rolling_checksum.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(EbHcrboTest, UnchangedFileFullReuse) {
  const std::string data = test::MakeSyntheticData(2 * 1024 * 1024, 5);
  EbHcrboChunker chunker;
  std::vector<ChunkDescriptor> full;
  CfiIndex cfi;
  EbHcrboStats stats{};
  ASSERT_TRUE(chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                                data.size(), &full, &cfi, &stats)
                  .ok());

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
  for (const auto& c : incr) {
    EXPECT_TRUE(c.reused_from_cfi);
  }
}

TEST(EbHcrboTest, OneByteEditHighReuse) {
  std::string data = test::MakeSyntheticData(20 * 1024 * 1024, 8);
  EbHcrboChunker chunker;
  std::vector<ChunkDescriptor> full;
  CfiIndex cfi;
  ASSERT_TRUE(chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                                data.size(), &full, &cfi, nullptr)
                  .ok());
  ASSERT_GE(full.size(), 20u) << "need enough chunks for 95% threshold";

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

TEST(EbHcrboTest, IncrementalBackupVerify) {
  const std::string repo = test::TempDir("hcrbo_incr_repo");
  const std::string source = test::TempDir("hcrbo_incr_source");
  ASSERT_TRUE(BackupEngine::InitRepo(repo).ok());

  std::string payload = test::MakeSyntheticData(10 * 1024 * 1024, 2);
  test::WriteFile(source + "/payload.bin", payload);

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull).ok());
  ASSERT_TRUE(engine.Verify().ok());

  payload[1024] ^= 0x80;
  test::WriteFile(source + "/payload.bin", payload);

  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());
  ASSERT_TRUE(engine.Verify().ok());
  EXPECT_GE(engine.stats().chunks_reused_from_cfi, 1u);
}

TEST(EbHcrboTest, RollingChecksumFastReject) {
  const std::string data = test::MakeSyntheticData(2 * 1024 * 1024, 11);
  EbHcrboChunker chunker;
  std::vector<ChunkDescriptor> full;
  CfiIndex cfi;
  ASSERT_TRUE(chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                                data.size(), &full, &cfi, nullptr)
                  .ok());
  ASSERT_FALSE(cfi.anchors.empty());
  for (auto& a : cfi.anchors) {
    a.rolling_checksum = RollingChecksum(
        reinterpret_cast<const uint8_t*>(data.data()) + a.offset, a.length);
  }

  std::string mutated = data;
  mutated[1024 * 1024] ^= 0x55;
  std::vector<ChunkDescriptor> incr;
  CfiIndex cfi_out;
  EbHcrboStats incr_stats{};
  ASSERT_TRUE(
      chunker
          .ChunkIncremental(reinterpret_cast<const uint8_t*>(mutated.data()),
                            mutated.size(), cfi, &incr, &cfi_out, &incr_stats)
          .ok());
  EXPECT_LT(incr_stats.chunks_reused_from_cfi, full.size());
}

TEST(EbHcrboTest, IncrementalReuseUnchanged) {
  const std::string data = test::MakeSyntheticData(4 * 1024 * 1024, 12);
  EbHcrboChunker chunker;
  std::vector<ChunkDescriptor> full;
  CfiIndex cfi;
  ASSERT_TRUE(chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                                data.size(), &full, &cfi, nullptr)
                  .ok());
  for (auto& a : cfi.anchors) {
    a.rolling_checksum = RollingChecksum(
        reinterpret_cast<const uint8_t*>(data.data()) + a.offset, a.length);
  }

  std::vector<ChunkDescriptor> incr;
  CfiIndex cfi_out;
  EbHcrboStats incr_stats{};
  ASSERT_TRUE(
      chunker
          .ChunkIncremental(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), cfi, &incr, &cfi_out, &incr_stats)
          .ok());
  EXPECT_EQ(incr_stats.chunks_reused_from_cfi, full.size());
}

TEST(EbHcrboTest, RollingChecksumPopulatesSkipHits) {
  const std::string data = test::MakeSyntheticData(2 * 1024 * 1024, 13);
  EbHcrboChunker chunker;
  std::vector<ChunkDescriptor> full;
  CfiIndex cfi;
  ASSERT_TRUE(chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                                data.size(), &full, &cfi, nullptr)
                  .ok());
  ASSERT_FALSE(cfi.anchors.empty());
  for (auto& a : cfi.anchors) {
    a.rolling_checksum = RollingChecksum(
        reinterpret_cast<const uint8_t*>(data.data()) + a.offset, a.length);
  }

  std::string mutated = data;
  mutated[512 * 1024] ^= 0x33;
  std::vector<ChunkDescriptor> incr;
  CfiIndex cfi_out;
  EbHcrboStats incr_stats{};
  ASSERT_TRUE(
      chunker
          .ChunkIncremental(reinterpret_cast<const uint8_t*>(mutated.data()),
                            mutated.size(), cfi, &incr, &cfi_out, &incr_stats)
          .ok());
  EXPECT_GT(incr_stats.cfi_rolling_skip_hits, 0u);
}

TEST(EbHcrboTest, OffsetIndexLargeAnchorCount) {
  constexpr size_t kAnchors = 10000;
  std::string data(kAnchors, 'x');
  EbHcrboChunker chunker;
  CfiIndex cfi;
  for (size_t i = 0; i < kAnchors; ++i) {
    ChunkAnchor a{};
    a.offset = i;
    a.length = 1;
    Sha256(reinterpret_cast<const uint8_t*>(&data[i]), 1, a.hash);
    a.rolling_checksum =
        RollingChecksum(reinterpret_cast<const uint8_t*>(&data[i]), 1);
    cfi.anchors.push_back(a);
  }

  data[5000] ^= 0x01;
  std::vector<ChunkDescriptor> incr;
  CfiIndex cfi_out;
  EbHcrboStats incr_stats{};
  ASSERT_TRUE(
      chunker
          .ChunkIncremental(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), cfi, &incr, &cfi_out, &incr_stats)
          .ok());
  EXPECT_GE(incr_stats.chunks_reused_from_cfi, 4900u);
  EXPECT_LT(incr_stats.chunks_reused_from_cfi, kAnchors);
}

}  // namespace
}  // namespace ebbackup
