#include "ebbackup/job/backup_window.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace ebbackup {
namespace job {

namespace {

bool ParseHm(const std::string& s, int* minutes_out) {
  if (!minutes_out || s.size() != 5 || s[2] != ':') return false;
  const int h = std::atoi(s.substr(0, 2).c_str());
  const int m = std::atoi(s.substr(3, 2).c_str());
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  *minutes_out = h * 60 + m;
  return true;
}

int MinutesSinceMidnightLocal(std::time_t now) {
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  return local.tm_hour * 60 + local.tm_min;
}

int SecondsSinceMidnightLocal(std::time_t now) {
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  return local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
}

bool ParseOptionalMinutes(const std::string& hhmm, int* out, bool* has) {
  if (!out || !has) return false;
  *has = !hhmm.empty();
  if (!*has) return true;
  return ParseHm(hhmm, out);
}

}  // namespace

bool HasBackupWindow(const BackupWindowPolicy& policy) {
  return !policy.window_start.empty() || !policy.window_end.empty();
}

bool IsWithinBackupWindow(std::time_t now, const BackupWindowPolicy& policy) {
  if (!HasBackupWindow(policy)) return true;

  int start_min = 0;
  int end_min = 0;
  bool has_start = false;
  bool has_end = false;
  if (!ParseOptionalMinutes(policy.window_start, &start_min, &has_start) ||
      !ParseOptionalMinutes(policy.window_end, &end_min, &has_end)) {
    return true;
  }

  const int now_min = MinutesSinceMidnightLocal(now);
  if (has_start && !has_end) return now_min >= start_min;
  if (!has_start && has_end) return now_min < end_min;
  if (start_min <= end_min) {
    return now_min >= start_min && now_min < end_min;
  }
  return now_min >= start_min || now_min < end_min;
}

int SecondsUntilWindowEnd(std::time_t now, const BackupWindowPolicy& policy) {
  if (policy.window_end.empty()) return INT_MAX;

  int start_min = 0;
  int end_min = 0;
  bool has_start = false;
  bool has_end = false;
  if (!ParseOptionalMinutes(policy.window_start, &start_min, &has_start) ||
      !ParseOptionalMinutes(policy.window_end, &end_min, &has_end) || !has_end) {
    return INT_MAX;
  }

  const int now_min = MinutesSinceMidnightLocal(now);
  const int now_sec = SecondsSinceMidnightLocal(now);
  const int end_sec = end_min * 60;

  const bool cross_midnight = has_start && start_min > end_min;
  if (cross_midnight) {
    if (now_min < end_min) {
      return std::max(0, end_sec - now_sec);
    }
    if (!has_start || now_min >= start_min) {
      return std::max(0, (24 * 3600 - now_sec) + end_sec);
    }
    return 0;
  }

  if (now_min < end_min) {
    return std::max(0, end_sec - now_sec);
  }
  return 0;
}

bool ShouldDowngradeDurability(std::time_t now,
                               const BackupWindowPolicy& policy) {
  if (!policy.durability_adaptive || policy.window_end.empty()) return false;
  const int grace = policy.deadline_grace_seconds > 0 ? policy.deadline_grace_seconds
                                                      : 300;
  const int remaining = SecondsUntilWindowEnd(now, policy);
  return remaining >= 0 && remaining <= grace;
}

int64_t WindowEndUnix(std::time_t now, const BackupWindowPolicy& policy) {
  if (policy.window_end.empty()) return 0;
  const int remaining = SecondsUntilWindowEnd(now, policy);
  if (remaining == INT_MAX) return 0;
  return static_cast<int64_t>(now) + static_cast<int64_t>(remaining);
}

}  // namespace job
}  // namespace ebbackup
