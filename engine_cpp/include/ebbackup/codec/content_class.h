#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/codec/codec_types.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

struct ContentClassStats {
  uint64_t incompressible_skips{0};
  uint64_t lz4_only{0};
  uint64_t zstd_attempts{0};
  uint64_t zstd_wins{0};
  uint64_t cpu_budget_spent_permille{0};
};

struct ContentEncodeRequest {
  const uint8_t* data{nullptr};
  size_t len{0};
  CompressMode mode{CompressMode::kOff};
  uint32_t cpu_budget_permille{1000};
  const char* path_hint{nullptr};
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

bool CpuBudgetTrySpend(uint32_t chunk_len, uint32_t* budget_permille);
uint32_t CpuBudgetZstdCost(size_t chunk_len);

Status ContentClassEncode(const ContentEncodeRequest& req,
                          ContentEncodeResult* out,
                          ContentClassStats* stats_delta);

}  // namespace ebbackup
