#include <gtest/gtest.h>

#include <filesystem>

#include "ebbackup/job/job_config.h"
#include "test_util.h"

namespace ebbackup {
namespace job {
namespace {

TEST(JobConfigTest, JobsToJsonRoundTrip) {
  BackupJob job{};
  job.id = "docs";
  job.name = "Documents";
  job.source_path = "E:/recoveryProjects/engine_cpp/test_output/job_cfg_source_123";
  job.retention_tag = 7;
  job.immutability_days = 3;
  job.exclude_globs = {"*.tmp", "*.log"};
  job.exclude_paths = {"node_modules", ".git"};
  const std::string json = JobsToJson({job});
  SCOPED_TRACE(json);
  std::vector<BackupJob> parsed;
  ASSERT_TRUE(ParseJobsJson(json, &parsed).ok());
  ASSERT_EQ(parsed.size(), 1u);
  EXPECT_EQ(parsed[0].id, "docs");
  EXPECT_EQ(parsed[0].name, "Documents");
  EXPECT_EQ(parsed[0].source_path, job.source_path);
  ASSERT_EQ(parsed[0].exclude_globs.size(), 2u);
  EXPECT_EQ(parsed[0].exclude_globs[0], "*.tmp");
  ASSERT_EQ(parsed[0].exclude_paths.size(), 2u);
  EXPECT_EQ(parsed[0].exclude_paths[0], "node_modules");
}

TEST(JobConfigTest, RoundTripCrud) {
  const std::string repo = test::TempDir("job_cfg_repo");
  const std::string source = test::TempDir("job_cfg_source");
  test::WriteFile(source + "/file.txt", "data");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  BackupJob job{};
  job.id = "docs";
  job.name = "Documents";
  job.source_path = source;
  job.retention_tag = 7;
  job.immutability_days = 3;
  job.exclude_globs = {"*.tmp", "*.log"};
  ASSERT_TRUE(UpsertJob(repo, job).ok());

  BackupJob loaded{};
  ASSERT_TRUE(GetJob(repo, "docs", &loaded).ok());
  EXPECT_EQ(loaded.id, "docs");
  EXPECT_EQ(loaded.name, "Documents");
  EXPECT_EQ(loaded.source_path, source);
  EXPECT_EQ(loaded.retention_tag, 7u);
  EXPECT_EQ(loaded.immutability_days, 3);
  ASSERT_EQ(loaded.exclude_globs.size(), 2u);
  EXPECT_EQ(loaded.exclude_globs[0], "*.tmp");

  std::vector<BackupJob> all;
  ASSERT_TRUE(LoadJobs(repo, &all).ok());
  ASSERT_EQ(all.size(), 1u);

  ASSERT_TRUE(DeleteJob(repo, "docs").ok());
  ASSERT_FALSE(GetJob(repo, "docs", &loaded).ok());
}

TEST(JobConfigTest, RejectsMissingSource) {
  const std::string repo = test::TempDir("job_cfg_repo2");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  BackupJob job{};
  job.id = "x";
  job.source_path = repo + "/missing";
  EXPECT_FALSE(UpsertJob(repo, job).ok());
}

TEST(JobConfigTest, WindowFieldsRoundTrip) {
  BackupJob job{};
  job.id = "nightly";
  job.name = "Nightly";
  job.source_path = "C:/data";
  job.window.window_start = "02:00";
  job.window.window_end = "06:00";
  job.window.deadline_grace_seconds = 120;
  job.window.durability_adaptive = true;
  const std::string json = JobsToJson({job});
  std::vector<BackupJob> parsed;
  ASSERT_TRUE(ParseJobsJson(json, &parsed).ok());
  ASSERT_EQ(parsed.size(), 1u);
  EXPECT_EQ(parsed[0].window.window_start, "02:00");
  EXPECT_EQ(parsed[0].window.window_end, "06:00");
  EXPECT_EQ(parsed[0].window.deadline_grace_seconds, 120);
  EXPECT_TRUE(parsed[0].window.durability_adaptive);
}

TEST(JobConfigTest, ParseJobsJsonArray) {
  std::vector<BackupJob> jobs;
  const std::string json =
      R"([{"id":"a","name":"A","source_path":"C:/x","retention_tag":1,"immutability_days":0,"worm":false,"exclude_globs":[]}])";
  ASSERT_TRUE(ParseJobsJson(json, &jobs).ok());
  ASSERT_EQ(jobs.size(), 1u);
  EXPECT_EQ(jobs[0].id, "a");
}

}  // namespace
}  // namespace job
}  // namespace ebbackup
