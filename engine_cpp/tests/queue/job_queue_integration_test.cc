#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/job/job_config.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(JobQueueIntegrationTest, EnqueueTwoJobsDrainCreatesTwoSnapshots) {
  const std::string repo = test::TempDir("jq_int_repo");
  const std::string source_a = test::TempDir("jq_int_src_a");
  const std::string source_b = test::TempDir("jq_int_src_b");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  std::error_code ec;
  std::filesystem::create_directories(source_a + "/a", ec);
  std::filesystem::create_directories(source_b + "/b", ec);
  test::WriteFile(source_a + "/a/one.txt", "one");
  test::WriteFile(source_b + "/b/two.txt", "two");

  job::BackupJob job_a{};
  job_a.id = "job_a";
  job_a.source_path = source_a;
  job::BackupJob job_b{};
  job_b.id = "job_b";
  job_b.source_path = source_b;
  ASSERT_TRUE(job::UpsertJob(repo, job_a).ok());
  ASSERT_TRUE(job::UpsertJob(repo, job_b).ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.EnqueueJob("job_a").ok());
  ASSERT_TRUE(engine.EnqueueJob("job_b", true).ok());
  ASSERT_TRUE(engine.RunJobQueue(true).ok());

  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(engine.ListSnapshots(&snaps).ok());
  EXPECT_GE(snaps.size(), 2u);

  const std::string status = engine.JobQueueStatusJson();
  EXPECT_NE(status.find("\"pending_count\":0"), std::string::npos);
}

}  // namespace
}  // namespace ebbackup
