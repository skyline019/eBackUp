#include "ebbackup/codec/content_class.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "ebbackup/codec/lz4_codec.h"
#include "ebbackup/codec/zstd_codec.h"

namespace ebbackup {

namespace {

constexpr size_t kSampleSize = 4096;

bool EndsWithIgnoreCase(const char* path, const char* suffix) {
  if (!path || !suffix) return false;
  const size_t path_len = std::strlen(path);
  const size_t suffix_len = std::strlen(suffix);
  if (path_len < suffix_len) return false;
  const char* start = path + path_len - suffix_len;
  for (size_t i = 0; i < suffix_len; ++i) {
    if (std::tolower(static_cast<unsigned char>(start[i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

bool PathHintIncompressible(const char* path_hint) {
  if (!path_hint) return false;
  static const char* kExts[] = {".jpg",  ".jpeg", ".png",  ".gif",  ".webp",
                                ".mp4",  ".mkv",  ".avi",  ".zip",  ".gz",
                                ".bz2",  ".xz",   ".7z",   ".rar",  ".mp3",
                                ".aac",  ".flac", ".wasm", ".pdf",  ".exe",
                                ".dll",  nullptr};
  for (const char** ext = kExts; *ext; ++ext) {
    if (EndsWithIgnoreCase(path_hint, *ext)) return true;
  }
  return false;
}

double ApproxEntropyPermille(const uint8_t* data, size_t len) {
  size_t freq[256]{};
  for (size_t i = 0; i < len; ++i) ++freq[data[i]];
  double entropy = 0.0;
  for (int i = 0; i < 256; ++i) {
    if (freq[i] == 0) continue;
    const double p = static_cast<double>(freq[i]) / static_cast<double>(len);
    entropy -= p * std::log2(p);
  }
  return entropy / 8.0 * 1000.0;
}

}  // namespace

ContentDataClass ClassifyContent(const uint8_t* data, size_t len,
                                 const char* path_hint) {
  if (PathHintIncompressible(path_hint)) {
    return ContentDataClass::kIncompressible;
  }
  if (!data || len == 0) return ContentDataClass::kIncompressible;

  const size_t sample = std::min(len, kSampleSize);
  bool seen[256]{};
  size_t unique = 0;
  for (size_t i = 0; i < sample; ++i) {
    if (!seen[data[i]]) {
      seen[data[i]] = true;
      ++unique;
    }
  }
  const double unique_ratio =
      static_cast<double>(unique) / static_cast<double>(sample);
  const double entropy_permille = ApproxEntropyPermille(data, sample);

  if (unique_ratio > 0.95 && entropy_permille > 950) {
    return ContentDataClass::kIncompressible;
  }
  if (entropy_permille > 700) {
    return ContentDataClass::kSlowCompressible;
  }
  return ContentDataClass::kFastCompressible;
}

uint32_t CpuBudgetZstdCost(size_t chunk_len) {
  return static_cast<uint32_t>((chunk_len * 50) / (1024u * 1024u)) + 1u;
}

bool CpuBudgetTrySpend(uint32_t chunk_len, uint32_t* budget_permille) {
  if (!budget_permille) return false;
  const uint32_t cost = CpuBudgetZstdCost(chunk_len);
  if (*budget_permille < cost) return false;
  *budget_permille -= cost;
  return true;
}

Status ContentClassEncode(const ContentEncodeRequest& req,
                          ContentEncodeResult* out,
                          ContentClassStats* stats_delta) {
  if (!out) return Status::InvalidArgument("out is null");
  out->payload.clear();
  out->codec = ChunkCodec::kRaw;
  out->uncompressed_len = static_cast<uint32_t>(req.len);

  if (!req.data && req.len > 0) {
    return Status::InvalidArgument("data is null");
  }
  if (req.len == 0) return Status::Ok();

  CompressMode mode = req.mode;
  if (mode == CompressMode::kOff) {
    out->payload.assign(req.data, req.data + req.len);
    return Status::Ok();
  }

  const ContentDataClass cls =
      ClassifyContent(req.data, req.len, req.path_hint);

  if (mode == CompressMode::kAuto && cls == ContentDataClass::kIncompressible) {
    out->payload.assign(req.data, req.data + req.len);
    if (stats_delta) ++stats_delta->incompressible_skips;
    return Status::Ok();
  }

  if (mode == CompressMode::kZstd ||
      (mode == CompressMode::kAuto &&
       cls == ContentDataClass::kSlowCompressible)) {
    uint32_t budget = req.cpu_budget_permille;
    if (mode == CompressMode::kAuto && !CpuBudgetTrySpend(req.len, &budget)) {
      mode = CompressMode::kLz4;
    } else {
      if (stats_delta) ++stats_delta->zstd_attempts;
      ZstdEncodeResult zstd{};
      const Status st = ZstdCompress(req.data, req.len, 1, &zstd);
      if (!st.ok()) return st;
      if (zstd.compressed) {
        out->payload = std::move(zstd.payload);
        out->codec = ChunkCodec::kZstd;
        out->uncompressed_len = zstd.uncompressed_size;
        if (stats_delta) {
          ++stats_delta->zstd_wins;
          stats_delta->cpu_budget_spent_permille += CpuBudgetZstdCost(req.len);
        }
        return Status::Ok();
      }
    }
  }

  if (mode == CompressMode::kLz4 || mode == CompressMode::kAuto) {
    Lz4EncodeResult lz4{};
    const Status st = Lz4Compress(req.data, req.len, &lz4);
    if (!st.ok()) return st;
    if (lz4.compressed) {
      out->payload = std::move(lz4.payload);
      out->codec = ChunkCodec::kLz4;
      out->uncompressed_len = lz4.uncompressed_size;
      if (stats_delta) ++stats_delta->lz4_only;
      if (mode == CompressMode::kAuto &&
          cls == ContentDataClass::kSlowCompressible &&
          static_cast<double>(out->payload.size()) / req.len >= 0.92 &&
          req.cpu_budget_permille > 0) {
        uint32_t budget = req.cpu_budget_permille;
        if (CpuBudgetTrySpend(req.len, &budget)) {
          if (stats_delta) ++stats_delta->zstd_attempts;
          ZstdEncodeResult zstd{};
          if (ZstdCompress(req.data, req.len, 1, &zstd).ok() &&
              zstd.compressed &&
              zstd.payload.size() < out->payload.size()) {
            out->payload = std::move(zstd.payload);
            out->codec = ChunkCodec::kZstd;
            out->uncompressed_len = zstd.uncompressed_size;
            if (stats_delta) {
              ++stats_delta->zstd_wins;
              stats_delta->cpu_budget_spent_permille +=
                  CpuBudgetZstdCost(req.len);
            }
            return Status::Ok();
          }
        }
      }
      return Status::Ok();
    }
  }

  out->payload.assign(req.data, req.data + req.len);
  out->codec = ChunkCodec::kRaw;
  return Status::Ok();
}

}  // namespace ebbackup
