#include "ebbackup/codec/zstd_codec.h"

#include <zstd.h>

namespace ebbackup {

Status ZstdCompress(const uint8_t* in, size_t len, int level,
                    ZstdEncodeResult* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->compressed = false;
  out->payload.assign(in, in + len);
  out->uncompressed_size = static_cast<uint32_t>(len);
  if (!in || len == 0) return Status::Ok();

  const size_t bound = ZSTD_compressBound(len);
  if (ZSTD_isError(bound)) return Status::Internal("zstd bound failed");
  std::vector<uint8_t> compressed(bound);
  const size_t written =
      ZSTD_compress(compressed.data(), bound, in, len, level);
  if (ZSTD_isError(written)) return Status::Internal("zstd compress failed");
  if (written >= len) return Status::Ok();

  out->compressed = true;
  out->payload.assign(compressed.begin(), compressed.begin() + written);
  out->uncompressed_size = static_cast<uint32_t>(len);
  return Status::Ok();
}

Status ZstdDecompress(const uint8_t* in, size_t stored_len,
                      uint32_t uncompressed_len, std::vector<uint8_t>* out) {
  if (!in || !out) return Status::InvalidArgument("null argument");
  out->assign(uncompressed_len, 0);
  const size_t decoded =
      ZSTD_decompress(out->data(), uncompressed_len, in, stored_len);
  if (ZSTD_isError(decoded)) return Status::Corrupt("zstd decompress failed");
  if (decoded != uncompressed_len) {
    return Status::Corrupt("zstd size mismatch");
  }
  return Status::Ok();
}

}  // namespace ebbackup
