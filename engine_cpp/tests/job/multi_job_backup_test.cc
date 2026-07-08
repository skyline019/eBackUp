#include <gtest/gtest.h>

#include "ebbackup/catalog/snapshot_meta.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/job/job_config.h"
#include "ebbackup/report/backup_report.h"
#include "ebbackup/store/snapshot_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(MultiJobBackupTest, TwoJobsTwoTxns) {
  const std::string repo = test::TempDir("multi_job_repo");
  const std::string source_a = test::TempDir("multi_job_src_a");
  const std::string source_b = test::TempDir("multi_job_src_b");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source_a + "/a.txt", "aaa");
  test::WriteFile(source_b + "/b.txt", "bbb");

  job::BackupJob job_a{};
  job_a.id = "job_a";
  job_a.name = "Source A";
  job_a.source_path = source_a;
  job_a.retention_tag = 1;
  ASSERT_TRUE(job::UpsertJob(repo, job_a).ok());

  job::BackupJob job_b{};
  job_b.id = "job_b";
  job_b.name = "Source B";
  job_b.source_path = source_b;
  job_b.retention_tag = 2;
  ASSERT_TRUE(job::UpsertJob(repo, job_b).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunJob("job_a").ok());
  const uint64_t txn_a = engine.superblock().critical.txn_id;

  ASSERT_TRUE(engine.RunJob("job_b").ok());
  const uint64_t txn_b = engine.superblock().critical.txn_id;
  ASSERT_NE(txn_a, txn_b);

  report::BackupReport rep_a{};
  ASSERT_TRUE(report::LoadBackupReport(repo, txn_a, &rep_a).ok());
  EXPECT_EQ(rep_a.job_id, "job_a");
  EXPECT_EQ(rep_a.retention_tag, 1u);

  report::BackupReport rep_b{};
  ASSERT_TRUE(report::LoadBackupReport(repo, txn_b, &rep_b).ok());
  EXPECT_EQ(rep_b.job_id, "job_b");
  EXPECT_EQ(rep_b.retention_tag, 2u);

  catalog::SnapshotMetaRecord meta_a{};
  ASSERT_TRUE(catalog::FindSnapshotMeta(repo, txn_a, &meta_a).ok());
  EXPECT_EQ(meta_a.job_id, "job_a");

  catalog::SnapshotMetaRecord meta_b{};
  ASSERT_TRUE(catalog::FindSnapshotMeta(repo, txn_b, &meta_b).ok());
  EXPECT_EQ(meta_b.job_id, "job_b");
}

}  // namespace
}  // namespace ebbackup
