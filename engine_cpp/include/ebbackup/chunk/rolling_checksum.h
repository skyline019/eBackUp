#pragma once

#include <cstddef>
#include <cstdint>

namespace ebbackup {

uint32_t RollingChecksum(const uint8_t* data, size_t len);
uint32_t RollingChecksumSlide(uint32_t prev, uint8_t out_byte, uint8_t in_byte,
                              size_t window_len);

}  // namespace ebbackup
