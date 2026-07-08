#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/job/job_config.h"
#include "ebbackup/report/rpo_summary.h"
#include "ebbackup/store/snapshot_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(RpoSummaryTest, AlternatingJobBackupsAggregateFields) {
  const std::string repo = test::TempDir("rpo_repo");
  const std::string source_a = test::TempDir("rpo_src_a");
  const std::string source_b = test::TempDir("rpo_src_b");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source_a + "/a.txt", "job-a");
  test::WriteFile(source_b + "/b.txt", "job-b");

  job::BackupJob job_a{};
  job_a.id = "job_a";
  job_a.name = "Job A";
  job_a.source_path = source_a;
  job_a.retention_tag = 7;
  job::BackupJob job_b{};
  job_b.id = "job_b";
  job_b.name = "Job B";
  job_b.source_path = source_b;
  job_b.retention_tag = 3;
  ASSERT_TRUE(job::UpsertJob(repo, job_a).ok());
  ASSERT_TRUE(job::UpsertJob(repo, job_b).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunJob("job_a").ok());
  ASSERT_TRUE(engine.RunJob("job_b", BackupMode::kIncremental).ok());

  report::RpoSummaryReport report{};
  ASSERT_TRUE(report::BuildRpoSummary(engine, &report).ok());
  EXPECT_GE(report.snapshot_count, 2u);
  EXPECT_GT(report.last_success_txn, 0u);
  EXPECT_GT(report.last_success_unix, 0);
  EXPECT_GE(report.days_since_last_success, 0.0);
  ASSERT_EQ(report.jobs.size(), 2u);

  bool found_a = false;
  bool found_b = false;
  for (const auto& j : report.jobs) {
    if (j.job_id == "job_a") {
      found_a = true;
      EXPECT_TRUE(j.last_report_ok);
      EXPECT_EQ(j.retention_tag, 7u);
    }
    if (j.job_id == "job_b") {
      found_b = true;
      EXPECT_TRUE(j.last_report_ok);
      EXPECT_EQ(j.retention_tag, 3u);
    }
  }
  EXPECT_TRUE(found_a);
  EXPECT_TRUE(found_b);

  const std::string json = engine.RpoSummaryJson();
  EXPECT_NE(json.find("\"ok\":true"), std::string::npos);
  EXPECT_NE(json.find("\"job_a\""), std::string::npos);
  EXPECT_NE(json.find("\"job_b\""), std::string::npos);
}

}  // namespace
}  // namespace ebbackup
