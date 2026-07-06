#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {

constexpr uint32_t kLz4MinCompressBytes = 64;

struct Lz4EncodeResult {
  bool compressed{false};
  std::vector<uint8_t> payload;
  uint32_t uncompressed_size{0};
};

Status Lz4Compress(const uint8_t* in, size_t len, Lz4EncodeResult* out);
Status Lz4Decompress(const uint8_t* in, size_t stored_len,
                     uint32_t uncompressed_len, std::vector<uint8_t>* out);

}  // namespace ebbackup
