#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/job/job_config.h"
#include "test_util.h"

#ifndef EBTEST_EB_EXE
#error "EBTEST_EB_EXE must be defined"
#endif

namespace ebbackup {
namespace test {
namespace {

int RunEbCommand(const std::string& args) {
  const std::string cmd = std::string(EBTEST_EB_EXE) + " " + args;
  return std::system(cmd.c_str());
}

std::string QuotePath(const std::string& path) {
  return "\"" + path + "\"";
}

TEST(QueueCliTest, DrainTwoJobsCreatesSnapshots) {
  const std::string repo = TempDir("cli_queue_repo");
  const std::string source_a = TempDir("cli_queue_src_a");
  const std::string source_b = TempDir("cli_queue_src_b");
  ASSERT_TRUE(InitDefaultRepo(repo).ok());
  WriteFile(source_a + "/a.txt", "one");
  WriteFile(source_b + "/b.txt", "two");

  job::BackupJob job_a{};
  job_a.id = "job_a";
  job_a.source_path = source_a;
  job::BackupJob job_b{};
  job_b.id = "job_b";
  job_b.source_path = source_b;
  ASSERT_TRUE(job::UpsertJob(repo, job_a).ok());
  ASSERT_TRUE(job::UpsertJob(repo, job_b).ok());

  ASSERT_EQ(RunEbCommand("queue add " + QuotePath(repo) + " job_a"), 0);
  ASSERT_EQ(RunEbCommand("queue add " + QuotePath(repo) + " job_b --incremental"),
            0);
  ASSERT_EQ(RunEbCommand("queue drain " + QuotePath(repo)), 0);

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  std::vector<SnapshotEntry> snaps;
  ASSERT_TRUE(engine.ListSnapshots(&snaps).ok());
  EXPECT_GE(snaps.size(), 2u);
}

TEST(QueueCliTest, VerifyChainAndRpoSummarySmoke) {
  const std::string repo = TempDir("cli_v24_repo");
  const std::string source = TempDir("cli_v24_src");
  ASSERT_TRUE(InitDefaultRepo(repo).ok());
  WriteFile(source + "/data.txt", "cli-v24");

  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  ASSERT_EQ(RunEbCommand("verify-chain " + QuotePath(repo)), 0);
  ASSERT_EQ(RunEbCommand("rpo-summary " + QuotePath(repo) + " --json"), 0);

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  EXPECT_NE(engine.SnapshotReachabilityJson(0).find("\"reachable\":true"),
            std::string::npos);
  EXPECT_NE(engine.RpoSummaryJson().find("\"ok\":true"), std::string::npos);
}

TEST(QueueCliTest, OrphanExplainAndAuditOpsSmoke) {
  const std::string repo = TempDir("cli_v25_repo");
  const std::string source = TempDir("cli_v25_src");
  ASSERT_TRUE(InitDefaultRepo(repo).ok());
  WriteFile(source + "/data.txt", "cli-v25");

  ASSERT_EQ(RunEbCommand("backup " + QuotePath(repo) + " " + QuotePath(source)), 0);
  ASSERT_EQ(RunEbCommand("orphan-explain " + QuotePath(repo) + " --json"), 0);
  ASSERT_EQ(RunEbCommand("audit-ops list " + QuotePath(repo) + " --json"), 0);

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  EXPECT_NE(engine.OrphanExplainJson(16).find("\"ok\":true"), std::string::npos);
  EXPECT_NE(engine.ListOpsAuditJson().find("\"ok\":true"), std::string::npos);
}

}  // namespace
}  // namespace test
}  // namespace ebbackup
