#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/status.h"

namespace ebbackup {

constexpr uint32_t kTopoCdcAlgoId = 2;

enum class TopoCdcVariant : uint8_t { kHom = 0, kTri = 1, kChain = 2 };

struct TopoCdcConfig {
  uint32_t min_size{64 * 1024};
  uint32_t avg_size{256 * 1024};
  uint32_t max_size{1024 * 1024};
  uint32_t window_w{64};
  uint32_t table_seed{0};
  uint16_t topo_calib_permille{0};
  uint8_t topo_shift{1};
  DigestAlgo digest_algo{DigestAlgo::kLegacy};
  TopoCdcVariant variant{TopoCdcVariant::kHom};
  uint8_t chain_stride_log{12};
  uint8_t chain_quant_q{0};
  uint32_t chain_lfsr_seed{0};
  bool chain_enable_beta1{false};
  uint8_t tri_k_points{16};
  uint32_t tri_q_mod{65521};
};

class TopoCdcSlice {
 public:
  explicit TopoCdcSlice(TopoCdcConfig config = {});

  Status Chunk(const uint8_t* data, size_t len,
               std::vector<ChunkDescriptor>* out) const;

  Status ChunkCuts(const uint8_t* data, size_t len, std::vector<size_t>* offsets,
                   std::vector<uint32_t>* lengths) const;

  const TopoCdcConfig& config() const { return config_; }

 private:
  TopoCdcConfig config_;
  uint32_t gear_[256]{};
};

bool CdcTopoCdcEnabled();
bool CdcTopoChainEnabled();
bool CdcTopoTriVariantRequested();

}  // namespace ebbackup
