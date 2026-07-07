#include <gtest/gtest.h>

#include <fstream>

#include "ebbackup/daemon/backup_daemon.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ScheduleSmokeTest, SingleRunUsesCurrentRepo) {
  const std::string source = test::TempDir("sched_source");
  const std::string repo_base = test::TempDir("sched_repos");
  test::WriteFile(source + "/sched.txt", "scheduled");

  ScheduleConfig cfg{};
  cfg.interval_seconds = 1;
  cfg.source_path = source;
  cfg.repo_base = repo_base;
  cfg.retain_count = 2;
  ASSERT_TRUE(RunScheduledBackup(cfg, 1).ok());

  const std::string repo = ScheduleRepoPath(repo_base);
  ASSERT_TRUE(std::filesystem::exists(repo + "/superblock.bin"));
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  EXPECT_TRUE(RepoUsesPersistentIndex(engine.superblock()));
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(ScheduleSmokeTest, SecondRunIsIncremental) {
  const std::string source = test::TempDir("sched_incr_source");
  const std::string repo_base = test::TempDir("sched_incr_repos");
  test::WriteFile(source + "/v1.txt", "version-one");

  ScheduleConfig cfg{};
  cfg.interval_seconds = 0;
  cfg.source_path = source;
  cfg.repo_base = repo_base;
  cfg.retain_count = 2;
  ASSERT_TRUE(RunScheduledBackup(cfg, 1).ok());

  test::WriteFile(source + "/v2.txt", "version-two");
  ASSERT_TRUE(RunScheduledBackup(cfg, 1).ok());

  const std::string repo = ScheduleRepoPath(repo_base);
  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.Verify().ok());
}

TEST(ScheduleSmokeTest, PruneLegacyRotatedReposWhenPresent) {
  const std::string source = test::TempDir("sched_legacy_source");
  const std::string repo_base = test::TempDir("sched_legacy_repos");
  test::WriteFile(source + "/data.txt", "legacy-prune");

  std::error_code ec;
  std::filesystem::create_directories(repo_base + "/repo-00000000-000001", ec);
  std::filesystem::create_directories(repo_base + "/repo-00000000-000002", ec);
  std::filesystem::create_directories(repo_base + "/repo-00000000-000003", ec);

  ScheduleConfig cfg{};
  cfg.interval_seconds = 0;
  cfg.source_path = source;
  cfg.repo_base = repo_base;
  cfg.retain_count = 2;
  ASSERT_TRUE(RunScheduledBackup(cfg, 1).ok());

  int legacy_count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(repo_base, ec)) {
    if (!entry.is_directory()) continue;
    const auto name = entry.path().filename().string();
    if (name.rfind("repo-", 0) == 0) ++legacy_count;
  }
  EXPECT_EQ(legacy_count, 2);
  EXPECT_TRUE(std::filesystem::exists(ScheduleRepoPath(repo_base) + "/superblock.bin"));
}

}  // namespace
}  // namespace ebbackup
