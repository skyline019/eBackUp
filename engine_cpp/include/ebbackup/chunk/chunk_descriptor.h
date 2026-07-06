#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace ebbackup {

struct ChunkDescriptor {
  uint64_t offset{0};
  uint32_t length{0};
  uint8_t hash[32]{};
  bool reused_from_cfi{false};

  bool operator==(const ChunkDescriptor& other) const {
    return offset == other.offset && length == other.length &&
           reused_from_cfi == other.reused_from_cfi &&
           std::memcmp(hash, other.hash, 32) == 0;
  }
};

inline bool HashEqual(const uint8_t a[32], const uint8_t b[32]) {
  return std::memcmp(a, b, 32) == 0;
}

}  // namespace ebbackup
