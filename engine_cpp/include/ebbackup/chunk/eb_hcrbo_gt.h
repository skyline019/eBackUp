#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/cfi_index.h"
#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/chunk/eb_hcrbo.h"
#include "ebbackup/chunk/gt_cdc.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

struct EbHcrboGtConfig {
  GtCdcConfig gt{};
  uint32_t strong_anchor_bytes{64 * 1024};
  uint64_t rabin_mask{0xFFFF};
  DigestAlgo digest_algo{DigestAlgo::kLegacy};
};

class EbHcrboGtChunker {
 public:
  explicit EbHcrboGtChunker(EbHcrboGtConfig config = {});

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

  EbHcrboGtConfig config_;
  GtCdcSlice gt_;
};

}  // namespace ebbackup
