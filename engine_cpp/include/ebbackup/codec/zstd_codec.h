#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/common/status.h"

namespace ebbackup {

struct ZstdEncodeResult {
  bool compressed{false};
  std::vector<uint8_t> payload;
  uint32_t uncompressed_size{0};
};

Status ZstdCompress(const uint8_t* in, size_t len, int level,
                    ZstdEncodeResult* out);
Status ZstdDecompress(const uint8_t* in, size_t stored_len,
                      uint32_t uncompressed_len, std::vector<uint8_t>* out);

}  // namespace ebbackup
