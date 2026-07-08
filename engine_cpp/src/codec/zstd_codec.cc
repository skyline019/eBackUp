#include "ebbackup/codec/zstd_codec.h"

#include <memory>
#include <vector>

#include <zstd.h>

namespace ebbackup {

namespace {

Status CompressWithCtx(ZSTD_CCtx* ctx, const uint8_t* in, size_t len,
                       ZstdEncodeResult* out) {
  const size_t bound = ZSTD_compressBound(len);
  if (ZSTD_isError(bound)) return Status::Internal("zstd bound failed");
  std::vector<uint8_t> compressed(bound);
  const size_t written =
      ZSTD_compress2(ctx, compressed.data(), bound, in, len);
  if (ZSTD_isError(written)) return Status::Internal("zstd compress failed");
  if (written >= len) return Status::Ok();

  out->compressed = true;
  out->payload.assign(compressed.begin(), compressed.begin() + written);
  out->uncompressed_size = static_cast<uint32_t>(len);
  return Status::Ok();
}

Status CompressWithCDict(const uint8_t* in, size_t len,
                         const std::shared_ptr<const ZSTD_CDict_s>& cdict,
                         ZstdEncodeResult* out) {
  if (!cdict) return Status::Internal("zstd cdict is null");
  ZSTD_CCtx* ctx = ZSTD_createCCtx();
  if (!ctx) return Status::Internal("zstd createCCtx failed");

  const size_t bound = ZSTD_compressBound(len);
  if (ZSTD_isError(bound)) {
    ZSTD_freeCCtx(ctx);
    return Status::Internal("zstd bound failed");
  }
  std::vector<uint8_t> compressed(bound);
  const size_t written = ZSTD_compress_usingCDict(
      ctx, compressed.data(), bound, in, len, cdict.get());
  ZSTD_freeCCtx(ctx);
  if (ZSTD_isError(written)) return Status::Internal("zstd cdict compress failed");
  if (written >= len) return Status::Ok();

  out->compressed = true;
  out->used_dictionary = true;
  out->payload.assign(compressed.begin(), compressed.begin() + written);
  out->uncompressed_size = static_cast<uint32_t>(len);
  return Status::Ok();
}

}  // namespace

Status ZstdCompressEx(const uint8_t* in, size_t len,
                      const ZstdCompressOptions& opts, ZstdEncodeResult* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->compressed = false;
  out->used_dictionary = false;
  out->payload.assign(in, in + len);
  out->uncompressed_size = static_cast<uint32_t>(len);
  if (!in || len == 0) return Status::Ok();

  if (opts.cdict) {
    return CompressWithCDict(in, len, opts.cdict, out);
  }

  ZSTD_CCtx* ctx = ZSTD_createCCtx();
  if (!ctx) return Status::Internal("zstd createCCtx failed");

  Status result = Status::Ok();
  do {
    if (ZSTD_isError(ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel,
                                            opts.level))) {
      result = Status::Internal("zstd set level failed");
      break;
    }
    if (opts.enable_long_distance) {
      if (ZSTD_isError(ZSTD_CCtx_setParameter(
              ctx, ZSTD_c_enableLongDistanceMatching, 1))) {
        result = Status::Internal("zstd enable ldm failed");
        break;
      }
    }

    if (opts.dict && opts.dict_size > 0) {
      if (ZSTD_isError(
              ZSTD_CCtx_loadDictionary(ctx, opts.dict, opts.dict_size))) {
        result = Status::Internal("zstd load dictionary failed");
        break;
      }
      out->used_dictionary = true;
    }

    result = CompressWithCtx(ctx, in, len, out);
  } while (false);

  ZSTD_freeCCtx(ctx);
  return result;
}

Status ZstdCompress(const uint8_t* in, size_t len, int level,
                    ZstdEncodeResult* out) {
  ZstdCompressOptions opts{};
  opts.level = level;
  return ZstdCompressEx(in, len, opts, out);
}

Status ZstdDecompressEx(const uint8_t* in, size_t stored_len,
                        uint32_t uncompressed_len,
                        const ZstdDecompressOptions& opts,
                        std::vector<uint8_t>* out) {
  if (!in || !out) return Status::InvalidArgument("null argument");
  out->assign(uncompressed_len, 0);

  if (opts.ddict) {
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    if (!dctx) return Status::Internal("zstd createDCtx failed");
    const size_t decoded = ZSTD_decompress_usingDDict(
        dctx, out->data(), uncompressed_len, in, stored_len, opts.ddict.get());
    ZSTD_freeDCtx(dctx);
    if (ZSTD_isError(decoded)) return Status::Corrupt("zstd decompress failed");
    if (decoded != uncompressed_len) {
      return Status::Corrupt("zstd size mismatch");
    }
    return Status::Ok();
  }

  ZSTD_DCtx* dctx = ZSTD_createDCtx();
  if (!dctx) return Status::Internal("zstd createDCtx failed");
  if (opts.dict && opts.dict_size > 0) {
    if (ZSTD_isError(
            ZSTD_DCtx_loadDictionary(dctx, opts.dict, opts.dict_size))) {
      ZSTD_freeDCtx(dctx);
      return Status::Corrupt("zstd load dictionary failed");
    }
  }
  const size_t decoded =
      ZSTD_decompressDCtx(dctx, out->data(), uncompressed_len, in, stored_len);
  ZSTD_freeDCtx(dctx);
  if (ZSTD_isError(decoded)) return Status::Corrupt("zstd decompress failed");
  if (decoded != uncompressed_len) {
    return Status::Corrupt("zstd size mismatch");
  }
  return Status::Ok();
}

Status ZstdDecompress(const uint8_t* in, size_t stored_len,
                      uint32_t uncompressed_len, std::vector<uint8_t>* out) {
  return ZstdDecompressEx(in, stored_len, uncompressed_len, {}, out);
}

}  // namespace ebbackup
