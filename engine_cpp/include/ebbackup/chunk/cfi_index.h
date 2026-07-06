#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ebbackup {

enum class AnchorStrength : uint8_t { kWeak = 0, kStrong = 1 };

struct ChunkAnchor {
  uint64_t offset{0};
  uint32_t length{0};
  uint8_t hash[32]{};
  AnchorStrength strength{AnchorStrength::kWeak};
  uint32_t rolling_checksum{0};
};

struct CfiIndex {
  std::vector<ChunkAnchor> anchors;
};

inline bool AnchorEqual(const ChunkAnchor& a, const ChunkAnchor& b) {
  return a.offset == b.offset && a.length == b.length &&
         a.strength == b.strength &&
         std::memcmp(a.hash, b.hash, 32) == 0;
}

}  // namespace ebbackup
