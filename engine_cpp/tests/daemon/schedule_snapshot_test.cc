#include <gtest/gtest.h>

#include "ebbackup/daemon/backup_daemon.h"
#include "ebbackup/store/snapshot_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ScheduleSnapshotTest, TwoRunsCreateSnapshotsAndPrune) {
  const std::string source = test::TempDir("sched_snap_source");
  const std::string repo_base = test::TempDir("sched_snap_repos");
  test::WriteFile(source + "/a.txt", "one");

  ScheduleConfig cfg{};
  cfg.interval_seconds = 0;
  cfg.source_path = source;
  cfg.repo_base = repo_base;
  cfg.retention_policy.retain_min = 2;
  cfg.retention_policy.tiers.clear();
  cfg.auto_prune = true;
  ASSERT_TRUE(RunScheduledBackup(cfg, 1).ok());

  test::WriteFile(source + "/b.txt", "two");
  ASSERT_TRUE(RunScheduledBackup(cfg, 1).ok());

  const std::string repo = ScheduleRepoPath(repo_base);
  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  EXPECT_GE(snaps.size(), 1u);
  EXPECT_LE(snaps.size(), 2u);

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.Verify().ok());
}

}  // namespace
}  // namespace ebbackup
