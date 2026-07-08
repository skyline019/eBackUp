#include "ebsync/backoff.h"

#include <chrono>

namespace ebsync {

int64_t NowUnix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int64_t ComputeBackoffSeconds(int attempt) {
  if (attempt < 1) attempt = 1;
  int64_t secs = 1;
  for (int i = 1; i < attempt && secs < 300; ++i) secs *= 2;
  if (secs > 300) secs = 300;
  return secs;
}

}  // namespace ebsync
