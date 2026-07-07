#include <gtest/gtest.h>

#include <cstring>

#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/rolling_checksum.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

bool ChunksEqual(const std::vector<ChunkDescriptor>& a,
                 const std::vector<ChunkDescriptor>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].offset != b[i].offset || a[i].length != b[i].length ||
        a[i].reused_from_cfi != b[i].reused_from_cfi ||
        std::memcmp(a[i].hash, b[i].hash, 32) != 0) {
      return false;
    }
  }
  return true;
}

void PopulateRolling(const uint8_t* data, size_t len, CfiIndex* cfi) {
  for (auto& a : cfi->anchors) {
    if (a.offset + a.length <= len) {
      a.rolling_checksum = RollingChecksum(data + a.offset, a.length);
    }
  }
}

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
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

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

TEST(EbHcrboTest, RollingBatchSkipParity) {
  std::string data = test::MakeSyntheticData(20 * 1024 * 1024, 17);
  EbHcrboChunker chunker;
  std::vector<ChunkDescriptor> full;
  CfiIndex cfi;
  ASSERT_TRUE(chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                                data.size(), &full, &cfi, nullptr)
                  .ok());
  PopulateRolling(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                  &cfi);

  data[5 * 1024 * 1024] ^= 0x42;
  std::vector<ChunkDescriptor> incr;
  CfiIndex cfi_out;
  EbHcrboStats incr_stats{};
  ASSERT_TRUE(
      chunker
          .ChunkIncremental(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), cfi, &incr, &cfi_out, &incr_stats)
          .ok());

  std::string baseline = test::MakeSyntheticData(20 * 1024 * 1024, 17);
  baseline[5 * 1024 * 1024] ^= 0x42;
  std::vector<ChunkDescriptor> incr_repeat;
  CfiIndex cfi_out_repeat;
  EbHcrboStats repeat_stats{};
  ASSERT_TRUE(chunker
                  .ChunkIncremental(reinterpret_cast<const uint8_t*>(baseline.data()),
                                    baseline.size(), cfi, &incr_repeat,
                                    &cfi_out_repeat, &repeat_stats)
                  .ok());
  EXPECT_TRUE(ChunksEqual(incr, incr_repeat));
  EXPECT_EQ(incr_stats.chunks_reused_from_cfi,
            repeat_stats.chunks_reused_from_cfi);
  EXPECT_GE(incr_stats.cfi_rolling_skip_hits, 0u);

  std::string unchanged = test::MakeSyntheticData(20 * 1024 * 1024, 17);
  std::vector<ChunkDescriptor> incr_unchanged;
  CfiIndex cfi_out_unchanged;
  EbHcrboStats unchanged_stats{};
  ASSERT_TRUE(chunker
                  .ChunkIncremental(
                      reinterpret_cast<const uint8_t*>(unchanged.data()),
                      unchanged.size(), cfi, &incr_unchanged, &cfi_out_unchanged,
                      &unchanged_stats)
                  .ok());
  EXPECT_EQ(incr_unchanged.size(), full.size());
  EXPECT_EQ(unchanged_stats.chunks_reused_from_cfi, full.size());
}

TEST(EbHcrboTest, CfiBloomParity) {
  std::string data = test::MakeSyntheticData(20 * 1024 * 1024, 17);
  EbHcrboChunker chunker;
  std::vector<ChunkDescriptor> full;
  CfiIndex cfi;
  ASSERT_TRUE(chunker.ChunkFull(reinterpret_cast<const uint8_t*>(data.data()),
                                data.size(), &full, &cfi, nullptr)
                  .ok());
  PopulateRolling(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                  &cfi);

  data[5 * 1024 * 1024] ^= 0x42;
  std::vector<ChunkDescriptor> incr;
  CfiIndex cfi_out;
  EbHcrboStats incr_stats{};
  ASSERT_TRUE(
      chunker
          .ChunkIncremental(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), cfi, &incr, &cfi_out, &incr_stats)
          .ok());

  std::string unchanged = test::MakeSyntheticData(20 * 1024 * 1024, 17);
  std::vector<ChunkDescriptor> incr_unchanged;
  CfiIndex cfi_out_unchanged;
  EbHcrboStats unchanged_stats{};
  ASSERT_TRUE(chunker
                  .ChunkIncremental(
                      reinterpret_cast<const uint8_t*>(unchanged.data()),
                      unchanged.size(), cfi, &incr_unchanged, &cfi_out_unchanged,
                      &unchanged_stats)
                  .ok());
  EXPECT_EQ(incr_unchanged.size(), full.size());
  EXPECT_GE(unchanged_stats.chunks_reused_from_cfi,
            static_cast<uint64_t>(full.size() * 0.9));
  for (size_t i = 0; i < full.size(); ++i) {
    EXPECT_EQ(incr_unchanged[i].offset, full[i].offset);
    EXPECT_EQ(incr_unchanged[i].length, full[i].length);
    EXPECT_EQ(std::memcmp(incr_unchanged[i].hash, full[i].hash, 32), 0);
  }
}

}  // namespace
}  // namespace ebbackup
