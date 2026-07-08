#include <gtest/gtest.h>

#include <ctime>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/job/job_config.h"
#include "ebbackup/store/retention_policy.h"
#include "ebbackup/store/snapshot_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(PruneImmutabilityTest, ProtectedSnapshotSurvivesAggressivePrune) {
  const std::string repo = test::TempDir("prune_immut_repo");
  const std::string source = test::TempDir("prune_immut_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/file.txt", "payload");

  job::BackupJob job{};
  job.id = "protected";
  job.name = "Protected";
  job.source_path = source;
  job.immutability_days = 7;
  job.worm = true;
  ASSERT_TRUE(job::UpsertJob(repo, job).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunJob("protected").ok());
  const uint64_t txn = engine.superblock().critical.txn_id;

  RetentionPolicy policy{};
  policy.retain_min = 0;
  policy.tiers.clear();
  PruneReport report{};
  PruneOptions opts{};
  opts.authorized = true;
  ASSERT_TRUE(PruneSnapshots(repo, policy, false, &report, opts).ok());
  EXPECT_EQ(report.pruned_count, 0u);

  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(ListSnapshots(repo, &snaps).ok());
  ASSERT_EQ(snaps.size(), 1u);
  EXPECT_EQ(snaps[0].txn_id, txn);
}

TEST(PruneImmutabilityTest, ImmutableRepoRejectsDestructivePruneWithoutAudit) {
  const std::string repo = test::TempDir("prune_worm_repo");
  const std::string source = test::TempDir("prune_worm_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/file.txt", "payload");

  job::BackupJob job{};
  job.id = "worm";
  job.source_path = source;
  job.worm = true;
  ASSERT_TRUE(job::UpsertJob(repo, job).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunJob("worm").ok());

  RetentionPolicy policy{};
  policy.retain_min = 0;
  policy.tiers.clear();
  PruneReport report{};
  EXPECT_FALSE(engine.PruneSnapshots(policy, false, &report).ok());

  PruneOptions opts{};
  opts.authorized = true;
  ASSERT_TRUE(PruneSnapshots(repo, policy, false, &report, opts).ok());
}

}  // namespace
}  // namespace ebbackup
