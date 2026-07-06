#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

#include "ebbackup/daemon/backup_daemon.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(WatchSmokeTest, FileChangeTriggersIncrementalBackup) {
  const std::string source = test::TempDir("watch_source");
  const std::string repo = test::TempDir("watch_repo");
  test::WriteFile(source + "/seed.txt", "seed");

  ASSERT_TRUE(BackupEngine::InitRepo(repo).ok());
  {
    BackupEngine engine(repo);
    ASSERT_TRUE(engine.Open().ok());
    ASSERT_TRUE(engine.RunBackup(source, BackupMode::kFull).ok());
  }

  std::atomic<bool> watch_done{false};
  std::thread watcher([&]() {
    BackupOptions opts{};
    const Status st = RunWatchBackup(source, repo, opts, 300, 1);
    watch_done.store(st.ok());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  test::WriteFile(source + "/trigger.txt", "changed");

  watcher.join();
  EXPECT_TRUE(watch_done.load());

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.Verify().ok());
}

}  // namespace
}  // namespace ebbackup
