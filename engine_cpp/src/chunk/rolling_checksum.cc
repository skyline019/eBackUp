#include "ebbackup/chunk/rolling_checksum.h"

namespace ebbackup {

namespace {

constexpr uint32_t kRollingBase = 65521u;

}  // namespace

uint32_t RollingChecksum(const uint8_t* data, size_t len) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (size_t i = 0; i < len; ++i) {
    a = (a + data[i]) % kRollingBase;
    b = (b + a) % kRollingBase;
  }
  return (b << 16) | a;
}

uint32_t RollingChecksumSlide(uint32_t prev, uint8_t out_byte, uint8_t in_byte,
                              size_t window_len) {
  uint32_t a = prev & 0xFFFFu;
  uint32_t b = prev >> 16;
  a = (a + kRollingBase - out_byte + in_byte) % kRollingBase;
  const uint32_t sub =
      static_cast<uint32_t>((static_cast<uint64_t>(window_len) * out_byte) %
                            kRollingBase);
  b = (b + kRollingBase - sub + a) % kRollingBase;
  return (b << 16) | a;
}

}  // namespace ebbackup
