#pragma once

#include <cstdint>

namespace ebsync {

int64_t ComputeBackoffSeconds(int attempt);
int64_t NowUnix();

}  // namespace ebsync
