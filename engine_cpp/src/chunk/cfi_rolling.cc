#include "ebbackup/chunk/cfi_rolling.h"

namespace ebbackup {

uint32_t CfiRollingVerifier::VerifyAnchor(const uint8_t* data, size_t offset,
                                          size_t length, uint32_t expected_rc) {
  if (expected_rc == 0) return 0;

  if (has_prev_ && prev_offset_ == offset && prev_len_ == length) {
    ++recompute_avoided_;
    return prev_rc_;
  }

  const uint32_t rc = RollingChecksum(data + offset, length);
  has_prev_ = true;
  prev_offset_ = offset;
  prev_len_ = length;
  prev_rc_ = rc;
  return rc;
}

}  // namespace ebbackup
