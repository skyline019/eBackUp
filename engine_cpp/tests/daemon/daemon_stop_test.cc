#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "ebbackup/daemon/backup_daemon.h"
#include "ebbackup/engine/backup_engine.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(DaemonStopTest, JobQueueDrainExitsOnStop) {
  const std::string repo = test::TempDir("daemon_stop_repo");
  ASSERT_TRUE(test::InitDefaultRepo(repo).ok());

  ResetDaemonStop();
  std::thread worker([&]() {
    BackupOptions opts{};
    const Status st = RunJobQueueDrain(repo, opts, {}, -1, 5);
    EXPECT_TRUE(st.ok()) << st.message();
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  RequestDaemonStop();
  worker.join();
  EXPECT_TRUE(IsDaemonStopRequested());
  ResetDaemonStop();
}

}  // namespace
}  // namespace ebbackup
