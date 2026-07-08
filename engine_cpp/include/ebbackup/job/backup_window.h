#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace ebbackup {
namespace job {

struct BackupWindowPolicy {
  std::string window_start;
  std::string window_end;
  int deadline_grace_seconds{300};
  bool durability_adaptive{false};
};

bool HasBackupWindow(const BackupWindowPolicy& policy);
bool IsWithinBackupWindow(std::time_t now, const BackupWindowPolicy& policy);
int SecondsUntilWindowEnd(std::time_t now, const BackupWindowPolicy& policy);
bool ShouldDowngradeDurability(std::time_t now, const BackupWindowPolicy& policy);
int64_t WindowEndUnix(std::time_t now, const BackupWindowPolicy& policy);

}  // namespace job
}  // namespace ebbackup
