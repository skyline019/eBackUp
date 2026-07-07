#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ebbackup {

struct StreamSegmentView {
  const uint8_t* seg0{nullptr};
  size_t len0{0};
  const uint8_t* seg1{nullptr};
  size_t len1{0};

  size_t size() const { return len0 + len1; }

  uint8_t at(size_t index) const {
    if (index < len0) return seg0[index];
    return seg1[index - len0];
  }

  void copy_range(size_t offset, size_t length, uint8_t* out) const {
    if (!out || length == 0) return;
    size_t copied = 0;
    while (copied < length) {
      const size_t pos = offset + copied;
      const size_t n =
          (pos < len0) ? std::min(length - copied, len0 - pos)
                       : std::min(length - copied, len0 + len1 - pos);
      const uint8_t* src =
          (pos < len0) ? seg0 + pos : seg1 + (pos - len0);
      std::memcpy(out + copied, src, n);
      copied += n;
    }
  }

  bool region_in_seg1(size_t offset, size_t length) const {
    return offset >= len0 && offset + length <= len0 + len1;
  }

  const uint8_t* seg1_ptr(size_t offset) const { return seg1 + (offset - len0); }
};

}  // namespace ebbackup
