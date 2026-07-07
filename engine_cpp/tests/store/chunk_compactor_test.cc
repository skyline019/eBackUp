#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/chunk_compactor.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/repo_stats.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ChunkCompactorTest, DryRunReportsAmplRatio) {
  const std::string repo = test::TempDir("compact_dry");
  const std::string source = test::TempDir("compact_dry_src");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(256 * 1024, 1));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  ChunkStore store(repo + "/data/chunks");
  ASSERT_TRUE(store.Open().ok());
  const std::string payload = test::MakeSyntheticData(32 * 1024, 99);
  uint8_t hash[32];
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size(), hash)
                  .ok());

  CompactReport report{};
  ASSERT_TRUE(CompactChunkStore(repo, true, &report).ok());
  EXPECT_GT(report.physical_before, report.live_bytes);
  EXPECT_GT(report.ampl_ratio_before, 1.0);
  EXPECT_EQ(report.physical_after, report.physical_before);
}

TEST(ChunkCompactorTest, CompactShrinksPhysicalAndVerifyOk) {
  const std::string repo = test::TempDir("compact_apply");
  const std::string source = test::TempDir("compact_apply_src");
  const std::string dest = test::TempDir("compact_apply_dest");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(512 * 1024, 3));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  ChunkStore store(repo + "/data/chunks");
  ASSERT_TRUE(store.Open().ok());
  const std::string payload = test::MakeSyntheticData(32 * 1024, 99);
  uint8_t hash[32];
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size(), hash)
                  .ok());

  CompactReport report{};
  ASSERT_TRUE(CompactChunkStore(repo, false, &report).ok());
  EXPECT_LT(report.physical_after, report.physical_before);
  EXPECT_LE(report.ampl_ratio_after, 1.05);

  BackupEngine verify_engine(repo);
  ASSERT_TRUE(verify_engine.Open().ok());
  ASSERT_TRUE(verify_engine.Verify().ok());
  ASSERT_TRUE(verify_engine.Restore(dest).ok());
  EXPECT_TRUE(std::filesystem::exists(dest + "/data.bin"));
}

}  // namespace
}  // namespace ebbackup
