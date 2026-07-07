#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/retention_policy.h"
#include "ebbackup/store/snapshot_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(SnapshotGcTest, PruneDoesNotDropRetainedSnapshotChunks) {
  const std::string repo = test::TempDir("snap_gc");
  const std::string source = test::TempDir("snap_gc_src");

  ASSERT_TRUE(test::InitV03Repo(repo).ok());
  test::WriteFile(source + "/only_v1.txt", test::MakeSyntheticData(128 * 1024, 1));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  test::WriteFile(source + "/only_v2.txt", test::MakeSyntheticData(128 * 1024, 2));
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());

  test::WriteFile(source + "/only_v3.txt", test::MakeSyntheticData(128 * 1024, 3));
  ASSERT_TRUE(engine.RunBackup(source, BackupMode::kIncremental).ok());

  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  ASSERT_EQ(snaps.size(), 3u);
  const uint64_t txn2 = snaps[1].txn_id;

  RetentionPolicy policy{};
  policy.retain_min = 2;
  policy.tiers.clear();
  PruneReport report{};
  ASSERT_TRUE(PruneSnapshots(repo, policy, false, &report).ok());
  EXPECT_GE(report.pruned_count, 1u);
  EXPECT_TRUE(IsKeptByPolicy(snaps, policy, txn2));

  ASSERT_TRUE(engine.GcOrphans(false, nullptr, false).ok());

  RestoreOptions opts{};
  opts.snapshot_txn_id = txn2;
  const std::string dest = test::TempDir("snap_gc_dest");
  ASSERT_TRUE(engine.Restore(dest, opts).ok());
  EXPECT_TRUE(std::filesystem::exists(dest + "/only_v1.txt"));
  EXPECT_TRUE(std::filesystem::exists(dest + "/only_v2.txt"));
  EXPECT_FALSE(std::filesystem::exists(dest + "/only_v3.txt"));
}

}  // namespace
}  // namespace ebbackup
