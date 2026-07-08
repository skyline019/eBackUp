#include <gtest/gtest.h>

#include <filesystem>

#include "ebbackup/catalog/snapshot_reachability.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/snapshot_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(SnapshotReachabilityTest, CompleteRepoIsReachable) {
  const std::string repo = test::TempDir("reach_ok_repo");
  const std::string source = test::TempDir("reach_ok_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.txt", "reachable-content");

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(engine.ListSnapshots(&snaps).ok());
  ASSERT_EQ(snaps.size(), 1u);

  catalog::SnapshotReachabilityReport report{};
  ASSERT_TRUE(
      catalog::AnalyzeSnapshotReachability(engine, snaps[0].txn_id, &report).ok());
  EXPECT_TRUE(report.reachable);
  EXPECT_EQ(report.missing_chunk_count, 0u);
  EXPECT_GT(report.chunks_checked, 0u);

  const std::string json = engine.SnapshotReachabilityJson(snaps[0].txn_id);
  EXPECT_NE(json.find("\"reachable\":true"), std::string::npos);
}

TEST(SnapshotReachabilityTest, MissingChunksAreUnreachable) {
  const std::string repo = test::TempDir("reach_bad_repo");
  const std::string source = test::TempDir("reach_bad_src");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/data.txt", "missing-chunk-content");

  uint64_t txn = 0;
  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source).ok());

    std::vector<SnapshotEntry> snaps;
    ASSERT_TRUE(engine.ListSnapshots(&snaps).ok());
    ASSERT_EQ(snaps.size(), 1u);
    txn = snaps[0].txn_id;
  }

  std::error_code ec;
  const std::string packs_dir = repo + "/data/packs";
  const std::string chunks_path = repo + "/data/chunks";
  if (std::filesystem::exists(packs_dir)) {
    std::filesystem::remove_all(packs_dir, ec);
    ASSERT_FALSE(ec) << ec.message();
  } else if (std::filesystem::exists(chunks_path)) {
    if (std::filesystem::is_directory(chunks_path)) {
      std::filesystem::remove_all(chunks_path, ec);
    } else {
      std::filesystem::remove(chunks_path, ec);
    }
    ASSERT_FALSE(ec) << ec.message();
  } else {
    FAIL() << "no chunk storage path found under repo";
  }

  BackupEngine engine2(repo);
  ASSERT_TRUE(engine2.Open().ok());
  catalog::SnapshotReachabilityReport report{};
  ASSERT_TRUE(catalog::AnalyzeSnapshotReachability(engine2, txn, &report).ok());
  EXPECT_FALSE(report.reachable);
  EXPECT_GT(report.missing_chunk_count, 0u);
}

}  // namespace
}  // namespace ebbackup
