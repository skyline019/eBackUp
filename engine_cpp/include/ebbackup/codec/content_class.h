#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/codec/codec_types.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

class ZstdDictionary;
class ZstdDictTrainer;

struct ContentClassStats {
  uint64_t incompressible_skips{0};
  uint64_t lz4_only{0};
  uint64_t zstd_attempts{0};
  uint64_t zstd_wins{0};
  uint64_t cpu_budget_spent_permille{0};
  uint64_t bytes_before_compress{0};
  uint64_t bytes_after_compress{0};
  uint64_t dict_hits{0};
};

struct ContentEncodeRequest {
  const uint8_t* data{nullptr};
  size_t len{0};
  CompressMode mode{CompressMode::kOff};
  CompressTier tier{CompressTier::kFast};
  int compress_level{0};
  uint32_t cpu_budget_permille{1000};
  const char* path_hint{nullptr};
  const ZstdDictionary* zstd_dict{nullptr};
  ZstdDictTrainer* dict_trainer{nullptr};
};

struct ContentEncodeResult {
  std::vector<uint8_t> payload;
  ChunkCodec codec{ChunkCodec::kRaw};
  uint32_t uncompressed_len{0};
};

enum class ContentDataClass {
  kIncompressible = 0,
  kFastCompressible,
  kSlowCompressible,
};

ContentDataClass ClassifyContent(const uint8_t* data, size_t len,
                                 const char* path_hint);

bool CpuBudgetTrySpend(uint32_t chunk_len, uint32_t* budget_permille, int level);

Status ContentClassEncode(const ContentEncodeRequest& req,
                          ContentEncodeResult* out,
                          ContentClassStats* stats_delta);

}  // namespace ebbackup
