#pragma once

#include <cstdint>

namespace ebbackup {
namespace bench {

constexpr double kBytesPerMB = 1'000'000.0;
constexpr double kBytesPerMiB = 1'048'576.0;

inline double ThroughputMBps(uint64_t bytes, double seconds) {
  if (seconds <= 0.0) return 0.0;
  return static_cast<double>(bytes) / kBytesPerMB / seconds;
}

inline double ThroughputMiBps(uint64_t bytes, double seconds) {
  if (seconds <= 0.0) return 0.0;
  return static_cast<double>(bytes) / kBytesPerMiB / seconds;
}

inline double MiBpsToMBps(double mibps) {
  return mibps * kBytesPerMiB / kBytesPerMB;
}

}  // namespace bench
}  // namespace ebbackup
