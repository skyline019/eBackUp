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

constexpr uint32_t kTopoPhnAlgoId = 4;

enum class TopoPhnKernel : uint8_t { kTriNative = 5, kPhH0Native = 6 };

struct TopoPhnConfig {
  uint32_t min_size{64 * 1024};
  uint32_t avg_size{256 * 1024};
  uint32_t max_size{1024 * 1024};
  DigestAlgo digest_algo{DigestAlgo::kLegacy};
  TopoPhnKernel kernel{TopoPhnKernel::kTriNative};
  uint32_t table_seed{0};
  uint32_t event_stride{64};
  uint8_t k_points{16};
  uint32_t q_mod{1024};
  uint32_t flip_tau{1};
  uint8_t persist_delta{1};
  bool enable_persist_delta{true};
};

bool CdcTopoPhnEnabled();
bool CdcTopoPhnKernelIsPh();
TopoPhnKernel TopoPhnKernelFromEnv();

TopoPhnConfig TopoPhnConfigForProfile(ChunkProfileMode mode);
TopoPhnConfig TopoPhnConfigForFileSize(size_t file_size, ChunkProfileMode mode,
                                       DigestAlgo digest_algo);
TopoPhnConfig TopoPhnConfigForRepo(const BackupSuperBlock& sb, size_t file_size,
                                   ChunkProfileMode mode, DigestAlgo digest_algo);

class TopoPhnSlice {
 public:
  explicit TopoPhnSlice(TopoPhnConfig config = {});

  Status ChunkCuts(const uint8_t* data, size_t len, std::vector<size_t>* offsets,
                   std::vector<uint32_t>* lengths) const;

  const TopoPhnConfig& config() const { return config_; }

 private:
  TopoPhnConfig config_;
};

}  // namespace ebbackup
