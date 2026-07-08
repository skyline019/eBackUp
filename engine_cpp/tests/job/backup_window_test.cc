#include <gtest/gtest.h>

#include <ctime>

#include "ebbackup/job/backup_window.h"

namespace ebbackup {
namespace job {
namespace {

std::time_t LocalTimeOnDay(int hour, int minute, int second = 0) {
  std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  local.tm_hour = hour;
  local.tm_min = minute;
  local.tm_sec = second;
  return mktime(&local);
}

TEST(BackupWindowTest, SameDayWindow) {
  BackupWindowPolicy policy{};
  policy.window_start = "02:00";
  policy.window_end = "06:00";
  const std::time_t inside = LocalTimeOnDay(3, 30);
  const std::time_t outside = LocalTimeOnDay(7, 0);
  EXPECT_TRUE(IsWithinBackupWindow(inside, policy));
  EXPECT_FALSE(IsWithinBackupWindow(outside, policy));
}

TEST(BackupWindowTest, CrossMidnightWindow) {
  BackupWindowPolicy policy{};
  policy.window_start = "22:00";
  policy.window_end = "06:00";
  const std::time_t late = LocalTimeOnDay(23, 0);
  const std::time_t early = LocalTimeOnDay(5, 0);
  const std::time_t noon = LocalTimeOnDay(12, 0);
  EXPECT_TRUE(IsWithinBackupWindow(late, policy));
  EXPECT_TRUE(IsWithinBackupWindow(early, policy));
  EXPECT_FALSE(IsWithinBackupWindow(noon, policy));
}

TEST(BackupWindowTest, ShouldDowngradeNearDeadline) {
  BackupWindowPolicy policy{};
  policy.window_end = "06:00";
  policy.deadline_grace_seconds = 600;
  policy.durability_adaptive = true;
  const std::time_t far = LocalTimeOnDay(4, 0);
  const std::time_t near = LocalTimeOnDay(5, 55);
  EXPECT_FALSE(ShouldDowngradeDurability(far, policy));
  EXPECT_TRUE(ShouldDowngradeDurability(near, policy));
}

TEST(BackupWindowTest, JobConfigRoundTripFields) {
  BackupWindowPolicy policy{};
  policy.window_start = "01:00";
  policy.window_end = "05:00";
  policy.deadline_grace_seconds = 120;
  policy.durability_adaptive = true;
  EXPECT_TRUE(HasBackupWindow(policy));
  EXPECT_GT(WindowEndUnix(LocalTimeOnDay(2, 0), policy), 0);
}

}  // namespace
}  // namespace job
}  // namespace ebbackup
