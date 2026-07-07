#pragma once

#include <cstddef>
#include <cstdint>

#include "ebbackup/chunk/rolling_checksum.h"

namespace ebbackup {

class CfiRollingVerifier {
 public:
  void Reset() {
    has_prev_ = false;
    prev_rc_ = 0;
    prev_len_ = 0;
    prev_offset_ = 0;
    recompute_avoided_ = 0;
  }

  uint32_t VerifyAnchor(const uint8_t* data, size_t offset, size_t length,
                        uint32_t expected_rc);

  uint64_t recompute_avoided() const { return recompute_avoided_; }

 private:
  bool has_prev_{false};
  size_t prev_offset_{0};
  uint32_t prev_rc_{0};
  size_t prev_len_{0};
  uint64_t recompute_avoided_{0};
};

}  // namespace ebbackup
