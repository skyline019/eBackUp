#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/daemon/backup_daemon.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ScheduleSmokeTest, SingleRunCreatesRotatedRepo) {
  const std::string source = test::TempDir("sched_source");
  const std::string repo_base = test::TempDir("sched_repos");
  test::WriteFile(source + "/sched.txt", "scheduled");

  ScheduleConfig cfg{};
  cfg.interval_seconds = 1;
  cfg.source_path = source;
  cfg.repo_base = repo_base;
  cfg.retain_count = 2;
  ASSERT_TRUE(RunScheduledBackup(cfg, 1).ok());

  int repo_count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(repo_base)) {
    if (!entry.is_directory()) continue;
    if (entry.path().filename().string().rfind("repo-", 0) == 0) {
      ++repo_count;
      BackupEngine engine(entry.path().string());
      ASSERT_TRUE(engine.Open().ok());
      ASSERT_TRUE(engine.Verify().ok());
    }
  }
  EXPECT_EQ(repo_count, 1);
}

}  // namespace
}  // namespace ebbackup
