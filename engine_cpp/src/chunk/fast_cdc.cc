#include "ebbackup/chunk/fast_cdc.h"

#include "ebbackup/common/digest.h"

#include <algorithm>
#include <cstring>

namespace ebbackup {

namespace {

uint32_t BuildMask(uint32_t avg_size) {
  uint32_t bits = 0;
  uint32_t v = avg_size;
  while (v > 1) {
    v >>= 1;
    ++bits;
  }
  if (bits == 0) bits = 1;
  if (bits > 31) bits = 31;
  return (1u << bits) - 1u;
}

void InitGearTable(uint32_t gear[256]) {
  for (int i = 0; i < 256; ++i) {
    uint32_t h = static_cast<uint32_t>(i);
    for (int j = 0; j < 16; ++j) {
      h = (h >> 1) ^
          (static_cast<uint32_t>(-static_cast<int32_t>(h & 1u)) & 0xD0000001u);
    }
    gear[i] = h;
  }
}

uint32_t InitWindowHash(const uint8_t* data, size_t end, uint32_t w,
                        const uint32_t gear[256]) {
  uint32_t h = 0;
  const size_t start = end - w;
  for (size_t i = start; i < end; ++i) {
    h = (h << 1) + gear[data[i]];
  }
  return h;
}

}  // namespace

FastCdcSlice::FastCdcSlice(FastCdcConfig config) : config_(config) {
  InitGearTable(gear_);
}

uint32_t FastCdcSlice::Mask() const { return BuildMask(config_.avg_size); }

Status FastCdcSlice::Chunk(const uint8_t* data, size_t len,
                           std::vector<ChunkDescriptor>* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  if (len == 0) return Status::Ok();

  if (len <= config_.min_size) {
    ChunkDescriptor desc{};
    desc.offset = 0;
    desc.length = static_cast<uint32_t>(len);
    Sha256(data, len, desc.hash);
    out->push_back(desc);
    return Status::Ok();
  }

  const uint32_t mask = Mask();
  const uint32_t w = config_.window_size;
  size_t pos = 0;

  while (pos < len) {
    const size_t remaining = len - pos;
    if (remaining <= config_.min_size) {
      ChunkDescriptor desc{};
      desc.offset = pos;
      desc.length = static_cast<uint32_t>(remaining);
      Sha256(data + pos, remaining, desc.hash);
      out->push_back(desc);
      break;
    }

    const size_t scan_start = pos + config_.min_size;
    size_t cut = std::min(pos + config_.max_size, len);
    bool found = false;

    if (scan_start >= w && scan_start < cut) {
      uint32_t h = InitWindowHash(data, scan_start, w, gear_);
      for (size_t i = scan_start; i < cut; ++i) {
        if ((h & mask) == 0) {
          cut = i;
          found = true;
          break;
        }
        h = (h << 1) + gear_[data[i]] - gear_[data[i - w]];
      }
    }

    if (!found && remaining > config_.max_size) {
      cut = pos + config_.max_size;
    } else if (!found) {
      cut = len;
    }

    const size_t chunk_len = cut - pos;
    ChunkDescriptor desc{};
    desc.offset = pos;
    desc.length = static_cast<uint32_t>(chunk_len);
    Sha256(data + pos, chunk_len, desc.hash);
    out->push_back(desc);
    pos = cut;
  }

  return Status::Ok();
}

}  // namespace ebbackup
