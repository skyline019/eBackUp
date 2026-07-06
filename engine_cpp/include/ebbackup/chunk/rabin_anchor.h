#pragma once

#include <cstddef>
#include <cstdint>

namespace ebbackup {

class RabinAnchor {
 public:
  explicit RabinAnchor(uint64_t mask = 0xFFFF);

  void Reset();
  void Feed(uint8_t byte);
  bool IsAnchor() const;

  uint64_t fingerprint() const { return fp_; }

 private:
  uint64_t fp_{0};
  uint64_t mask_{0};
  static uint64_t Table(uint8_t byte);
};

bool RabinFindAnchor(const uint8_t* data, size_t len, size_t start, size_t end,
                     size_t window, uint64_t mask, size_t* cut_out);

}  // namespace ebbackup
