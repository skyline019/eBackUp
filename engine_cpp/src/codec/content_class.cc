#include "ebbackup/codec/content_class.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>

#include "ebbackup/codec/compress_policy.h"
#include "ebbackup/codec/lz4_codec.h"
#include "ebbackup/codec/zstd_codec.h"
#include "ebbackup/codec/zstd_dict.h"

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

void RecordCompressionStats(ContentClassStats* stats_delta, size_t before,
                            size_t after) {
  if (!stats_delta) return;
  stats_delta->bytes_before_compress += before;
  stats_delta->bytes_after_compress += after;
}

Status TryZstdEncode(const uint8_t* data, size_t len, int level,
                     bool enable_ldm, const ZstdDictionary* zstd_dict,
                     ZstdEncodeResult* zstd, ContentClassStats* stats_delta) {
  ZstdCompressOptions opts{};
  opts.level = level;
  opts.enable_long_distance = enable_ldm;
  if (zstd_dict && !zstd_dict->empty()) {
    if (auto cdict = zstd_dict->AcquireCDict(level)) {
      opts.cdict = std::move(cdict);
    } else {
      opts.dict = zstd_dict->data();
      opts.dict_size = zstd_dict->size();
    }
  }
  const Status st = ZstdCompressEx(data, len, opts, zstd);
  if (!st.ok()) return st;
  if (zstd->compressed && zstd->used_dictionary && stats_delta) {
    ++stats_delta->dict_hits;
  }
  return Status::Ok();
}

bool ShouldTryZstdFirst(CompressMode mode, ContentDataClass cls,
                        const TierZstdParams& tier_params) {
  if (mode == CompressMode::kZstd) return true;
  if (mode != CompressMode::kAuto) return false;
  if (cls == ContentDataClass::kSlowCompressible) return true;
  return !tier_params.prefer_lz4_for_fast;
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

bool CpuBudgetTrySpend(uint32_t chunk_len, uint32_t* budget_permille,
                       int level) {
  if (!budget_permille) return false;
  const uint32_t cost = CpuBudgetZstdCost(chunk_len, level);
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
  const TierZstdParams tier_params =
      ResolveTierParams(req.tier, req.compress_level);

  if (mode == CompressMode::kAuto && cls == ContentDataClass::kIncompressible) {
    out->payload.assign(req.data, req.data + req.len);
    if (stats_delta) ++stats_delta->incompressible_skips;
    return Status::Ok();
  }

  const int zstd_level = ResolveZstdLevel(cls, tier_params);
  const bool enable_ldm = ShouldEnableLdm(req.len, tier_params);

  auto apply_zstd_win = [&](ZstdEncodeResult&& zstd) {
    out->payload = std::move(zstd.payload);
    out->codec = ChunkCodec::kZstd;
    out->uncompressed_len = zstd.uncompressed_size;
    RecordCompressionStats(stats_delta, req.len, out->payload.size());
    if (req.dict_trainer) {
      req.dict_trainer->MaybeAddSample(req.data, req.len, cls);
    }
  };

  if (ShouldTryZstdFirst(mode, cls, tier_params)) {
    uint32_t budget = req.cpu_budget_permille;
    if (mode == CompressMode::kAuto &&
        !CpuBudgetTrySpend(static_cast<uint32_t>(req.len), &budget,
                           zstd_level)) {
      mode = CompressMode::kLz4;
    } else {
      if (stats_delta) ++stats_delta->zstd_attempts;
      ZstdEncodeResult zstd{};
      const Status st = TryZstdEncode(req.data, req.len, zstd_level, enable_ldm,
                                      req.zstd_dict, &zstd, stats_delta);
      if (!st.ok()) return st;
      if (zstd.compressed) {
        apply_zstd_win(std::move(zstd));
        if (stats_delta) {
          ++stats_delta->zstd_wins;
          stats_delta->cpu_budget_spent_permille +=
              CpuBudgetZstdCost(req.len, zstd_level);
        }
        return Status::Ok();
      }
    }
  }

  if (mode == CompressMode::kLz4 ||
      (mode == CompressMode::kAuto && tier_params.prefer_lz4_for_fast)) {
    Lz4EncodeResult lz4{};
    const Status st = Lz4Compress(req.data, req.len, &lz4);
    if (!st.ok()) return st;
    if (lz4.compressed) {
      out->payload = std::move(lz4.payload);
      out->codec = ChunkCodec::kLz4;
      out->uncompressed_len = lz4.uncompressed_size;
      if (stats_delta) ++stats_delta->lz4_only;
      RecordCompressionStats(stats_delta, req.len, out->payload.size());
      if (req.dict_trainer) {
        req.dict_trainer->MaybeAddSample(req.data, req.len, cls);
      }

      const double ratio =
          static_cast<double>(out->payload.size()) / static_cast<double>(req.len);
      if (mode == CompressMode::kAuto &&
          ratio >= tier_params.lz4_retry_ratio_threshold &&
          req.cpu_budget_permille > 0) {
        uint32_t budget = req.cpu_budget_permille;
        if (CpuBudgetTrySpend(static_cast<uint32_t>(req.len), &budget,
                              zstd_level)) {
          if (stats_delta) ++stats_delta->zstd_attempts;
          ZstdEncodeResult zstd{};
          if (TryZstdEncode(req.data, req.len, zstd_level, enable_ldm,
                            req.zstd_dict, &zstd, stats_delta)
                  .ok() &&
              zstd.compressed &&
              zstd.payload.size() < out->payload.size()) {
            apply_zstd_win(std::move(zstd));
            if (stats_delta) {
              ++stats_delta->zstd_wins;
              stats_delta->cpu_budget_spent_permille +=
                  CpuBudgetZstdCost(req.len, zstd_level);
            }
            return Status::Ok();
          }
        }
      }
      return Status::Ok();
    }
  }

  if (mode == CompressMode::kZstd ||
      (mode == CompressMode::kAuto &&
       cls == ContentDataClass::kSlowCompressible)) {
    uint32_t budget = req.cpu_budget_permille;
    if (mode == CompressMode::kAuto &&
        !CpuBudgetTrySpend(static_cast<uint32_t>(req.len), &budget,
                           zstd_level)) {
      out->payload.assign(req.data, req.data + req.len);
      out->codec = ChunkCodec::kRaw;
      RecordCompressionStats(stats_delta, req.len, req.len);
      return Status::Ok();
    }
    if (stats_delta) ++stats_delta->zstd_attempts;
    ZstdEncodeResult zstd{};
    const Status st = TryZstdEncode(req.data, req.len, zstd_level, enable_ldm,
                                    req.zstd_dict, &zstd, stats_delta);
    if (!st.ok()) return st;
    if (zstd.compressed) {
      apply_zstd_win(std::move(zstd));
      if (stats_delta) {
        ++stats_delta->zstd_wins;
        stats_delta->cpu_budget_spent_permille +=
            CpuBudgetZstdCost(req.len, zstd_level);
      }
      return Status::Ok();
    }
  }

  out->payload.assign(req.data, req.data + req.len);
  out->codec = ChunkCodec::kRaw;
  RecordCompressionStats(stats_delta, req.len, req.len);
  return Status::Ok();
}

}  // namespace ebbackup
