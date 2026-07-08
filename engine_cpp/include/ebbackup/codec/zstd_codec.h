#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "ebbackup/common/status.h"

struct ZSTD_CDict_s;
struct ZSTD_DDict_s;

namespace ebbackup {

struct ZstdCompressOptions {
  int level{1};
  bool enable_long_distance{false};
  const uint8_t* dict{nullptr};
  size_t dict_size{0};
  std::shared_ptr<const ZSTD_CDict_s> cdict{};
};

struct ZstdDecompressOptions {
  const uint8_t* dict{nullptr};
  size_t dict_size{0};
  std::shared_ptr<const ZSTD_DDict_s> ddict{};
};

struct ZstdEncodeResult {
  bool compressed{false};
  std::vector<uint8_t> payload;
  uint32_t uncompressed_size{0};
  bool used_dictionary{false};
};

Status ZstdCompressEx(const uint8_t* in, size_t len,
                      const ZstdCompressOptions& opts, ZstdEncodeResult* out);
Status ZstdCompress(const uint8_t* in, size_t len, int level,
                    ZstdEncodeResult* out);
Status ZstdDecompressEx(const uint8_t* in, size_t stored_len,
                        uint32_t uncompressed_len,
                        const ZstdDecompressOptions& opts,
                        std::vector<uint8_t>* out);
Status ZstdDecompress(const uint8_t* in, size_t stored_len,
                      uint32_t uncompressed_len, std::vector<uint8_t>* out);

}  // namespace ebbackup
