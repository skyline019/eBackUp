#include <gtest/gtest.h>

#include "ebbackup/winmeta/vss_session.h"

namespace ebbackup {
namespace winmeta {
namespace {

TEST(VssLifecycleTest, FinishBackupNoOpWhenInactive) {
  VssSession session;
  EXPECT_TRUE(session.FinishBackup().ok());
  EXPECT_FALSE(session.backup_finished());
  EXPECT_FALSE(session.active());
}

TEST(VssLifecycleTest, EndClearsInactiveSession) {
  VssSession session;
  EXPECT_TRUE(session.End().ok());
  EXPECT_FALSE(session.active());
}

#ifndef _WIN32
TEST(VssLifecycleTest, BeginRejectedOnNonWindows) {
  VssSession session;
  VssBeginOptions opts{};
  opts.mode = VssConsistencyMode::kApp;
  const Status st = session.Begin({"/tmp/source"}, opts);
  EXPECT_FALSE(st.ok());
  EXPECT_FALSE(session.active());
  EXPECT_FALSE(session.backup_finished());
}
#endif

}  // namespace
}  // namespace winmeta
}  // namespace ebbackup
