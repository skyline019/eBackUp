#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/repo_stats.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(RepoStatsTest, EmptyInitializedRepoWithoutManifest) {
  const std::string repo = test::TempDir("stats_empty");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());

  RepoStats stats{};
  ASSERT_TRUE(ComputeRepoStats(repo, &stats).ok());
  EXPECT_EQ(stats.manifest_bytes, 0u);
  EXPECT_EQ(stats.live_bytes, 0u);
  EXPECT_EQ(stats.physical_bytes, 0u);
  EXPECT_DOUBLE_EQ(stats.ampl_ratio, 1.0);
}

TEST(RepoStatsTest, CleanBackupLiveEqualsPhysical) {
  const std::string repo = test::TempDir("stats_clean");
  const std::string source = test::TempDir("stats_clean_src");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/file.txt", test::MakeSyntheticData(128 * 1024, 5));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  RepoStats stats{};
  ASSERT_TRUE(ComputeRepoStats(repo, &stats).ok());
  EXPECT_GT(stats.physical_bytes, 0u);
  EXPECT_EQ(stats.live_bytes, stats.physical_bytes);
  EXPECT_EQ(stats.orphan_bytes, 0u);
  EXPECT_GE(stats.unique_chunks, 1u);
  EXPECT_DOUBLE_EQ(stats.ampl_ratio, 1.0);
  EXPECT_GT(stats.manifest_bytes, 0u);
}

TEST(RepoStatsTest, OrphansIncreaseAmplRatio) {
  const std::string repo = test::TempDir("stats_orphan");
  const std::string source = test::TempDir("stats_orphan_src");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/file.txt", test::MakeSyntheticData(128 * 1024, 6));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  ChunkStore store(repo + "/data/chunks");
  ASSERT_TRUE(store.Open().ok());
  const std::string payload = "orphan-for-stats";
  uint8_t hash[32];
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size(), hash)
                  .ok());

  RepoStats stats{};
  ASSERT_TRUE(ComputeRepoStats(repo, &stats).ok());
  EXPECT_GT(stats.orphan_bytes, 0u);
  EXPECT_GT(stats.ampl_ratio, 1.0);
  EXPECT_GT(stats.physical_bytes, stats.live_bytes);
}

TEST(RepoStatsTest, EngineGetRepoStatsMatchesCompute) {
  const std::string repo = test::TempDir("stats_engine");
  const std::string source = test::TempDir("stats_engine_src");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/a.txt", "hello");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  RepoStats direct{};
  RepoStats via_engine{};
  ASSERT_TRUE(ComputeRepoStats(repo, &direct).ok());
  ASSERT_TRUE(engine.GetRepoStats(&via_engine).ok());
  EXPECT_EQ(direct.physical_bytes, via_engine.physical_bytes);
  EXPECT_EQ(direct.live_bytes, via_engine.live_bytes);
  EXPECT_EQ(direct.unique_chunks, via_engine.unique_chunks);
  EXPECT_EQ(direct.live_uncompressed_bytes, via_engine.live_uncompressed_bytes);
  EXPECT_EQ(direct.compress_ratio, via_engine.compress_ratio);
}

TEST(RepoStatsTest, CompressRatioVisibleForLiveChunks) {
  const std::string repo = test::TempDir("stats_compress");
  const std::string source = test::TempDir("stats_compress_src");
  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/compressible.txt",
                  std::string(512 * 1024, static_cast<char>('A')));

  BackupEngine engine(repo);
  BackupOptions opts{};
  opts.compress_mode = CompressMode::kZstd;
  opts.compress_tier = CompressTier::kBalanced;
  opts.use_zstd_dict = true;
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull, opts).ok());

  RepoStats stats{};
  ASSERT_TRUE(ComputeRepoStats(repo, &stats).ok());
  EXPECT_GT(stats.live_uncompressed_bytes, 0u);
  EXPECT_GT(stats.live_stored_payload_bytes, 0u);
  EXPECT_LT(stats.compress_ratio, 1.0);
  EXPECT_LT(stats.live_stored_payload_bytes, stats.live_uncompressed_bytes);
  EXPECT_GE(stats.compressed_chunk_count, 1u);
}

}  // namespace
}  // namespace ebbackup
