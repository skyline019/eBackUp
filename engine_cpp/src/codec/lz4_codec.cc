#include "ebbackup/codec/lz4_codec.h"

#include <lz4.h>

namespace ebbackup {

Status Lz4Compress(const uint8_t* in, size_t len, Lz4EncodeResult* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->compressed = false;
  out->payload.assign(in, in + len);
  out->uncompressed_size = static_cast<uint32_t>(len);
  if (!in || len == 0) return Status::Ok();
  if (len < kLz4MinCompressBytes) return Status::Ok();

  const int bound = LZ4_compressBound(static_cast<int>(len));
  if (bound <= 0) return Status::Internal("lz4 bound failed");
  std::vector<uint8_t> compressed(static_cast<size_t>(bound));
  const int written = LZ4_compress_default(
      reinterpret_cast<const char*>(in), reinterpret_cast<char*>(compressed.data()),
      static_cast<int>(len), bound);
  if (written <= 0) return Status::Internal("lz4 compress failed");
  if (static_cast<size_t>(written) >= len) return Status::Ok();

  out->compressed = true;
  out->payload.assign(compressed.begin(), compressed.begin() + written);
  out->uncompressed_size = static_cast<uint32_t>(len);
  return Status::Ok();
}

Status Lz4Decompress(const uint8_t* in, size_t stored_len,
                     uint32_t uncompressed_len, std::vector<uint8_t>* out) {
  if (!in || !out) return Status::InvalidArgument("null argument");
  out->assign(uncompressed_len, 0);
  const int decoded = LZ4_decompress_safe(
      reinterpret_cast<const char*>(in), reinterpret_cast<char*>(out->data()),
      static_cast<int>(stored_len), static_cast<int>(uncompressed_len));
  if (decoded < 0) return Status::Corrupt("lz4 decompress failed");
  if (static_cast<uint32_t>(decoded) != uncompressed_len) {
    return Status::Corrupt("lz4 size mismatch");
  }
  return Status::Ok();
}

}  // namespace ebbackup
