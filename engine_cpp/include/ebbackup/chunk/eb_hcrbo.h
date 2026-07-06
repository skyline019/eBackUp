#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/cfi_index.h"
#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

struct EbHcrboConfig {
  FastCdcConfig fast{};
  uint32_t strong_anchor_bytes{64 * 1024};
  uint64_t rabin_mask{0xFFFF};
};

struct EbHcrboStats {
  uint64_t chunks_reused_from_cfi{0};
  uint64_t cfi_rolling_skip_hits{0};
  uint64_t chunks_cut_fastcdc{0};
  uint64_t chunks_cut_rabin{0};
};

class EbHcrboChunker {
 public:
  explicit EbHcrboChunker(EbHcrboConfig config = {});

  Status ChunkFull(const uint8_t* data, size_t len,
                   std::vector<ChunkDescriptor>* out, CfiIndex* cfi_out,
                   EbHcrboStats* stats = nullptr) const;

  Status ChunkIncremental(const uint8_t* data, size_t len,
                          const CfiIndex& history,
                          std::vector<ChunkDescriptor>* out,
                          CfiIndex* cfi_out,
                          EbHcrboStats* stats = nullptr) const;

 private:
  Status ChunkRegion(const uint8_t* data, size_t len, size_t region_start,
                     size_t region_end, bool use_rabin_edges,
                     std::vector<ChunkDescriptor>* out, CfiIndex* cfi_out,
                     EbHcrboStats* stats) const;

  EbHcrboConfig config_;
  FastCdcSlice fast_;
};

}  // namespace ebbackup
