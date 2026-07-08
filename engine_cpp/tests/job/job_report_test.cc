#include <gtest/gtest.h>

#include "ebbackup/catalog/job_report.h"
#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/job/job_config.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(JobReportTest, AppendAndListPerJob) {
  const std::string repo = test::TempDir("job_report_repo");
  const std::string source_a = test::TempDir("job_report_src_a");
  const std::string source_b = test::TempDir("job_report_src_b");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  test::WriteFile(source_a + "/a.txt", "aaa");
  test::WriteFile(source_b + "/b.txt", "bbb");

  job::BackupJob job_a{};
  job_a.id = "job_a";
  job_a.name = "Source A";
  job_a.source_path = source_a;
  ASSERT_TRUE(job::UpsertJob(repo, job_a).ok());

  job::BackupJob job_b{};
  job_b.id = "job_b";
  job_b.name = "Source B";
  job_b.source_path = source_b;
  ASSERT_TRUE(job::UpsertJob(repo, job_b).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunJob("job_a").ok());
  test::WriteFile(source_a + "/a2.txt", "more");
  ASSERT_TRUE(engine.RunJob("job_a", BackupMode::kIncremental).ok());
  ASSERT_TRUE(engine.RunJob("job_b").ok());

  std::vector<catalog::JobReportLine> job_a_reports;
  ASSERT_TRUE(catalog::ListJobReports(repo, "job_a", 0, 100, &job_a_reports).ok());
  EXPECT_EQ(job_a_reports.size(), 2u);

  std::vector<catalog::JobReportLine> job_b_reports;
  ASSERT_TRUE(catalog::ListJobReports(repo, "job_b", 0, 100, &job_b_reports).ok());
  EXPECT_EQ(job_b_reports.size(), 1u);

  std::vector<catalog::JobReportLine> page;
  ASSERT_TRUE(catalog::ListJobReports(repo, "job_a", 1, 1, &page).ok());
  EXPECT_EQ(page.size(), 1u);
  EXPECT_NE(page[0].txn_id, job_a_reports[0].txn_id);

  const std::string json =
      catalog::JobReportsToJson(job_a_reports, job_a_reports.size(), 0);
  EXPECT_NE(json.find("\"ok\":true"), std::string::npos);
  EXPECT_NE(json.find("\"reuse_pct\""), std::string::npos);
}

}  // namespace
}  // namespace ebbackup
