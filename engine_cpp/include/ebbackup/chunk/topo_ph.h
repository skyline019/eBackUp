#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ebbackup/chunk/chunk_descriptor.h"
#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/common/digest.h"
#include "ebbackup/common/status.h"
#include "ebbackup/state/superblock.h"

namespace ebbackup {

constexpr uint32_t kTopoPhAlgoId = 3;

enum class TopoPhKernel : uint8_t { kTriV2 = 3, kPhH0 = 4 };

struct TopoPhConfig {
  uint32_t min_size{64 * 1024};
  uint32_t avg_size{256 * 1024};
  uint32_t max_size{1024 * 1024};
  uint32_t window_w{64};
  uint32_t table_seed{0};
  uint16_t topo_calib_permille{0};
  uint8_t topo_shift{1};
  DigestAlgo digest_algo{DigestAlgo::kLegacy};
  TopoPhKernel kernel{TopoPhKernel::kTriV2};
  uint8_t k_points{16};
  uint32_t q_mod{1024};
};

bool CdcTopoPhEnabled();
bool CdcTopoPhKernelIsPh();
TopoPhKernel TopoPhKernelFromEnv();

TopoPhConfig TopoPhConfigForProfile(ChunkProfileMode mode);
TopoPhConfig TopoPhConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                     DigestAlgo digest_algo);
TopoPhConfig TopoPhConfigForRepo(const BackupSuperBlock& sb, size_t file_size,
                                 ChunkProfileMode mode, DigestAlgo digest_algo);

class TopoPhSlice {
 public:
  explicit TopoPhSlice(TopoPhConfig config = {});

  Status ChunkCuts(const uint8_t* data, size_t len, std::vector<size_t>* offsets,
                   std::vector<uint32_t>* lengths) const;

  const TopoPhConfig& config() const { return config_; }

 private:
  TopoPhConfig config_;
  uint32_t gear_[256]{};
};

}  // namespace ebbackup
