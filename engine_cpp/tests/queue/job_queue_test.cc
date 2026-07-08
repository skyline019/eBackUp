#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/job/job_config.h"
#include "ebbackup/queue/job_queue.h"
#include "test_util.h"

namespace ebbackup {
namespace queue {
namespace {

TEST(JobQueueTest, EnqueueAndPersistenceRoundtrip) {
  const std::string repo = test::TempDir("jq_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  JobQueue q(repo);
  ASSERT_TRUE(q.Load().ok());
  JobQueueEnqueueOptions opts;
  opts.incremental = true;
  opts.flags = 1;
  ASSERT_TRUE(q.Enqueue("job_a", opts).ok());
  ASSERT_TRUE(q.Enqueue("job_b").ok());
  EXPECT_EQ(q.pending_count(), 2u);
  ASSERT_TRUE(q.Save().ok());

  JobQueue loaded(repo);
  ASSERT_TRUE(loaded.Load().ok());
  ASSERT_EQ(loaded.pending_count(), 2u);
  EXPECT_EQ(loaded.pending()[0].job_id, "job_a");
  EXPECT_TRUE(loaded.pending()[0].incremental);
  EXPECT_EQ(loaded.pending()[1].job_id, "job_b");
}

TEST(JobQueueTest, RunNextInvokesRunJob) {
  const std::string repo = test::TempDir("jq_run_repo");
  const std::string source = test::TempDir("jq_run_source");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());
  test::WriteFile(source + "/file.txt", "payload");

  job::BackupJob job{};
  job.id = "docs";
  job.source_path = source;
  ASSERT_TRUE(job::UpsertJob(repo, job).ok());

  JobQueue q(repo);
  ASSERT_TRUE(q.Enqueue("docs").ok());
  ASSERT_TRUE(q.Save().ok());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  JobQueueRunReport report;
  ASSERT_TRUE(q.RunNext(&engine, {}, &report).ok());
  EXPECT_EQ(report.job_id, "docs");
  EXPECT_TRUE(report.run_status.ok());
  EXPECT_EQ(q.pending_count(), 0u);
}

}  // namespace
}  // namespace queue
}  // namespace ebbackup
